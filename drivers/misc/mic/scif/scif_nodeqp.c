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
#include "../bus/scif_bus.h"
#include "scif_peer_bus.h"
#include "scif_main.h"
#include "scif_nodeqp.h"
#include "scif_map.h"

/*
 ************************************************************************
 * SCIF node Queue Pair (QP) setup flow:
 *
 * 1) SCIF driver gets probed with a scif_hw_dev via the scif_hw_bus
 * 2) scif_setup_qp(..) allocates the local qp and calls
 *	scif_setup_qp_connect(..) which allocates and maps the local
 *	buffer for the inbound QP
 * 3) The local node updates the device page with the DMA address of the QP
 * 4) A delayed work is scheduled (qp_dwork) which periodically reads if
 *	the peer node has updated its QP DMA address
 * 5) Once a valid non zero address is found in the QP DMA address field
 *	in the device page, the local node maps the remote node's QP,
 *	updates its outbound QP and sends a SCIF_INIT message to the peer
 * 6) The SCIF_INIT message is received by the peer node QP interrupt bottom
 *	half handler by calling scif_init(..)
 * 7) scif_init(..) registers a new SCIF peer node by calling
 *	scif_peer_register_device(..) which signifies the addition of a new
 *	SCIF node
 * 8) On the mgmt node, P2P network setup/teardown is initiated if all the
 *	remote nodes are online via scif_p2p_setup(..)
 * 9) For P2P setup, the host maps the remote nodes' aperture and memory
 *	bars and sends a SCIF_NODE_ADD message to both nodes
 * 10) As part of scif_nodeadd, both nodes set up their local inbound
 *	QPs and send a SCIF_NODE_ADD_ACK to the mgmt node
 * 11) As part of scif_node_add_ack(..) the mgmt node forwards the
 *	SCIF_NODE_ADD_ACK to the remote nodes
 * 12) As part of scif_node_add_ack(..) the remote nodes update their
 *	outbound QPs, make sure they can access memory on the remote node
 *	and then add a new SCIF peer node by calling
 *	scif_peer_register_device(..) which signifies the addition of a new
 *	SCIF node.
 * 13) The SCIF network is now established across all nodes.
 *
 ************************************************************************
 * SCIF node QP teardown flow (initiated by non mgmt node):
 *
 * 1) SCIF driver gets a remove callback with a scif_hw_dev via the scif_hw_bus
 * 2) The device page QP DMA address field is updated with 0x0
 * 3) A non mgmt node now cleans up all local data structures and sends a
 *	SCIF_EXIT message to the peer and waits for a SCIF_EXIT_ACK
 * 4) As part of scif_exit(..) handling scif_disconnect_node(..) is called
 * 5) scif_disconnect_node(..) sends a SCIF_NODE_REMOVE message to all the
 *	peers and waits for a SCIF_NODE_REMOVE_ACK
 * 6) As part of scif_node_remove(..) a remote node unregisters the peer
 *	node from the SCIF network and sends a SCIF_NODE_REMOVE_ACK
 * 7) When the mgmt node has received all the SCIF_NODE_REMOVE_ACKs
 *	it sends itself a node remove message whose handling cleans up local
 *	data structures and unregisters the peer node from the SCIF network
 * 8) The mgmt node sends a SCIF_EXIT_ACK
 * 9) Upon receipt of the SCIF_EXIT_ACK the node initiating the teardown
 *	completes the SCIF remove routine
 * 10) The SCIF network is now torn down for the node initiating the
 *	teardown sequence
 *
 ************************************************************************
 * SCIF node QP teardown flow (initiated by mgmt node):
 *
 * 1) SCIF driver gets a remove callback with a scif_hw_dev via the scif_hw_bus
 * 2) The device page QP DMA address field is updated with 0x0
 * 3) The mgmt node calls scif_disconnect_node(..)
 * 4) scif_disconnect_node(..) sends a SCIF_NODE_REMOVE message to all the peers
 *	and waits for a SCIF_NODE_REMOVE_ACK
 * 5) As part of scif_node_remove(..) a remote node unregisters the peer
 *	node from the SCIF network and sends a SCIF_NODE_REMOVE_ACK
 * 6) When the mgmt node has received all the SCIF_NODE_REMOVE_ACKs
 *	it unregisters the peer node from the SCIF network
 * 7) The mgmt node sends a SCIF_EXIT message and waits for a SCIF_EXIT_ACK.
 * 8) A non mgmt node upon receipt of a SCIF_EXIT message calls scif_stop(..)
 *	which would clean up local data structures for all SCIF nodes and
 *	then send a SCIF_EXIT_ACK back to the mgmt node
 * 9) Upon receipt of the SCIF_EXIT_ACK the the mgmt node sends itself a node
 *	remove message whose handling cleans up local data structures and
 *	destroys any P2P mappings.
 * 10) The SCIF hardware device for which a remove callback was received is now
 *	disconnected from the SCIF network.
 */
/*
 * Initializes "local" data structures for the QP. Allocates the QP
 * ring buffer (rb) and initializes the "in bound" queue.
 */
int scif_setup_qp_connect(struct scif_qp *qp, dma_addr_t *qp_offset,
			  int local_size, struct scif_dev *scifdev)
{
	void *local_q = NULL;
	int err = 0;
	u32 tmp_rd = 0;

	spin_lock_init(&qp->send_lock);
	spin_lock_init(&qp->recv_lock);

	local_q = kzalloc(local_size, GFP_KERNEL);
	if (!local_q) {
		err = -ENOMEM;
		return err;
	}
	err = scif_map_single(&qp->local_buf, local_q, scifdev, local_size);
	if (err)
		goto kfree;
	/*
	 * To setup the inbound_q, the buffer lives locally, the read pointer
	 * is remote and the write pointer is local.
	 */
	scif_rb_init(&qp->inbound_q,
		     &tmp_rd,
		     &qp->local_write,
		     local_q, get_count_order(local_size));
	/*
	 * The read pointer is NULL initially and it is unsafe to use the ring
	 * buffer til this changes!
	 */
	qp->inbound_q.read_ptr = NULL;
	err = scif_map_single(qp_offset, qp,
			      scifdev, sizeof(struct scif_qp));
	if (err)
		goto unmap;
	qp->local_qp = *qp_offset;
	return err;
unmap:
	scif_unmap_single(qp->local_buf, scifdev, local_size);
	qp->local_buf = 0;
kfree:
	kfree(local_q);
	return err;
}

/* When the other side has already done it's allocation, this is called */
int scif_setup_qp_accept(struct scif_qp *qp, dma_addr_t *qp_offset,
			 dma_addr_t phys, int local_size,
			 struct scif_dev *scifdev)
{
	void *local_q;
	void *remote_q;
	struct scif_qp *remote_qp;
	int remote_size;
	int err = 0;

	spin_lock_init(&qp->send_lock);
	spin_lock_init(&qp->recv_lock);
	/* Start by figuring out where we need to point */
	remote_qp = scif_ioremap(phys, sizeof(struct scif_qp), scifdev);
	if (!remote_qp)
		return -EIO;
	qp->remote_qp = remote_qp;
	if (qp->remote_qp->magic != SCIFEP_MAGIC) {
		err = -EIO;
		goto iounmap;
	}
	qp->remote_buf = remote_qp->local_buf;
	remote_size = qp->remote_qp->inbound_q.size;
	remote_q = scif_ioremap(qp->remote_buf, remote_size, scifdev);
	if (!remote_q) {
		err = -EIO;
		goto iounmap;
	}
	qp->remote_qp->local_write = 0;
	/*
	 * To setup the outbound_q, the buffer lives in remote memory,
	 * the read pointer is local, the write pointer is remote
	 */
	scif_rb_init(&qp->outbound_q,
		     &qp->local_read,
		     &qp->remote_qp->local_write,
		     remote_q,
		     get_count_order(remote_size));
	local_q = kzalloc(local_size, GFP_KERNEL);
	if (!local_q) {
		err = -ENOMEM;
		goto iounmap_1;
	}
	err = scif_map_single(&qp->local_buf, local_q, scifdev, local_size);
	if (err)
		goto kfree;
	qp->remote_qp->local_read = 0;
	/*
	 * To setup the inbound_q, the buffer lives locally, the read pointer
	 * is remote and the write pointer is local
	 */
	scif_rb_init(&qp->inbound_q,
		     &qp->remote_qp->local_read,
		     &qp->local_write,
		     local_q, get_count_order(local_size));
	err = scif_map_single(qp_offset, qp, scifdev,
			      sizeof(struct scif_qp));
	if (err)
		goto unmap;
	qp->local_qp = *qp_offset;
	return err;
unmap:
	scif_unmap_single(qp->local_buf, scifdev, local_size);
	qp->local_buf = 0;
kfree:
	kfree(local_q);
iounmap_1:
	scif_iounmap(remote_q, remote_size, scifdev);
	qp->outbound_q.rb_base = NULL;
iounmap:
	scif_iounmap(qp->remote_qp, sizeof(struct scif_qp), scifdev);
	qp->remote_qp = NULL;
	return err;
}

int scif_setup_qp_connect_response(struct scif_dev *scifdev,
				   struct scif_qp *qp, u64 payload)
{
	int err = 0;
	void *r_buf;
	int remote_size;
	phys_addr_t tmp_phys;

	qp->remote_qp = scif_ioremap(payload, sizeof(struct scif_qp), scifdev);

	if (!qp->remote_qp) {
		err = -ENOMEM;
		goto error;
	}

	if (qp->remote_qp->magic != SCIFEP_MAGIC) {
		dev_err(&scifdev->sdev->dev,
			"SCIFEP_MAGIC mismatch between self %d remote %d\n",
			scif_dev[scif_info.nodeid].node, scifdev->node);
		err = -ENODEV;
		goto error;
	}

	tmp_phys = qp->remote_qp->local_buf;
	remote_size = qp->remote_qp->inbound_q.size;
	r_buf = scif_ioremap(tmp_phys, remote_size, scifdev);

	if (!r_buf)
		return -EIO;

	qp->local_read = 0;
	scif_rb_init(&qp->outbound_q,
		     &qp->local_read,
		     &qp->remote_qp->local_write,
		     r_buf,
		     get_count_order(remote_size));
	/*
	 * resetup the inbound_q now that we know where the
	 * inbound_read really is.
	 */
	scif_rb_init(&qp->inbound_q,
		     &qp->remote_qp->local_read,
		     &qp->local_write,
		     qp->inbound_q.rb_base,
		     get_count_order(qp->inbound_q.size));
error:
	return err;
}

static __always_inline void
scif_send_msg_intr(struct scif_dev *scifdev)
{
	struct scif_hw_dev *sdev = scifdev->sdev;

	if (scifdev_is_p2p(scifdev))
		sdev->hw_ops->send_p2p_intr(sdev, scifdev->rdb, &scifdev->mmio);
	else
		sdev->hw_ops->send_intr(sdev, scifdev->rdb);
}

int scif_qp_response(phys_addr_t phys, struct scif_dev *scifdev)
{
	int err = 0;
	struct scifmsg msg;

	err = scif_setup_qp_connect_response(scifdev, scifdev->qpairs, phys);
	if (!err) {
		/*
		 * Now that everything is setup and mapped, we're ready
		 * to tell the peer about our queue's location
		 */
		msg.uop = SCIF_INIT;
		msg.dst.node = scifdev->node;
		err = scif_nodeqp_send(scifdev, &msg);
	}
	return err;
}

void scif_send_exit(struct scif_dev *scifdev)
{
	struct scifmsg msg;
	int ret;

	scifdev->exit = OP_IN_PROGRESS;
	msg.uop = SCIF_EXIT;
	msg.src.node = scif_info.nodeid;
	msg.dst.node = scifdev->node;
	ret = scif_nodeqp_send(scifdev, &msg);
	if (ret)
		goto done;
	/* Wait for a SCIF_EXIT_ACK message */
	wait_event_timeout(scif_info.exitwq, scifdev->exit == OP_COMPLETED,
			   SCIF_NODE_ALIVE_TIMEOUT);
done:
	scifdev->exit = OP_IDLE;
}

int scif_setup_qp(struct scif_dev *scifdev)
{
	int err = 0;
	int local_size;
	struct scif_qp *qp;

	local_size = SCIF_NODE_QP_SIZE;

	qp = kzalloc(sizeof(*qp), GFP_KERNEL);
	if (!qp) {
		err = -ENOMEM;
		return err;
	}
	qp->magic = SCIFEP_MAGIC;
	scifdev->qpairs = qp;
	err = scif_setup_qp_connect(qp, &scifdev->qp_dma_addr,
				    local_size, scifdev);
	if (err)
		goto free_qp;
	/*
	 * We're as setup as we can be. The inbound_q is setup, w/o a usable
	 * outbound q.  When we get a message, the read_ptr will be updated,
	 * and we will pull the message.
	 */
	return err;
free_qp:
	kfree(scifdev->qpairs);
	scifdev->qpairs = NULL;
	return err;
}

static void scif_p2p_freesg(struct scatterlist *sg)
{
	kfree(sg);
}

static struct scatterlist *
scif_p2p_setsg(phys_addr_t pa, int page_size, int page_cnt)
{
	struct scatterlist *sg;
	struct page *page;
	int i;

	sg = kcalloc(page_cnt, sizeof(struct scatterlist), GFP_KERNEL);
	if (!sg)
		return NULL;
	sg_init_table(sg, page_cnt);
	for (i = 0; i < page_cnt; i++) {
		page = pfn_to_page(pa >> PAGE_SHIFT);
		sg_set_page(&sg[i], page, page_size, 0);
		pa += page_size;
	}
	return sg;
}

/* Init p2p mappings required to access peerdev from scifdev */
static struct scif_p2p_info *
scif_init_p2p_info(struct scif_dev *scifdev, struct scif_dev *peerdev)
{
	struct scif_p2p_info *p2p;
	int num_mmio_pages, num_aper_pages, sg_page_shift, err, num_aper_chunks;
	struct scif_hw_dev *psdev = peerdev->sdev;
	struct scif_hw_dev *sdev = scifdev->sdev;

	num_mmio_pages = psdev->mmio->len >> PAGE_SHIFT;
	num_aper_pages = psdev->aper->len >> PAGE_SHIFT;

	p2p = kzalloc(sizeof(*p2p), GFP_KERNEL);
	if (!p2p)
		return NULL;
	p2p->ppi_sg[SCIF_PPI_MMIO] = scif_p2p_setsg(psdev->mmio->pa,
						    PAGE_SIZE, num_mmio_pages);
	if (!p2p->ppi_sg[SCIF_PPI_MMIO])
		goto free_p2p;
	p2p->sg_nentries[SCIF_PPI_MMIO] = num_mmio_pages;
	sg_page_shift = get_order(min(psdev->aper->len, (u64)(1 << 30)));
	num_aper_chunks = num_aper_pages >> (sg_page_shift - PAGE_SHIFT);
	p2p->ppi_sg[SCIF_PPI_APER] = scif_p2p_setsg(psdev->aper->pa,
						    1 << sg_page_shift,
						    num_aper_chunks);
	p2p->sg_nentries[SCIF_PPI_APER] = num_aper_chunks;
	err = dma_map_sg(&sdev->dev, p2p->ppi_sg[SCIF_PPI_MMIO],
			 num_mmio_pages, PCI_DMA_BIDIRECTIONAL);
	if (err != num_mmio_pages)
		goto scif_p2p_free;
	err = dma_map_sg(&sdev->dev, p2p->ppi_sg[SCIF_PPI_APER],
			 num_aper_chunks, PCI_DMA_BIDIRECTIONAL);
	if (err != num_aper_chunks)
		goto dma_unmap;
	p2p->ppi_da[SCIF_PPI_MMIO] = sg_dma_address(p2p->ppi_sg[SCIF_PPI_MMIO]);
	p2p->ppi_da[SCIF_PPI_APER] = sg_dma_address(p2p->ppi_sg[SCIF_PPI_APER]);
	p2p->ppi_len[SCIF_PPI_MMIO] = num_mmio_pages;
	p2p->ppi_len[SCIF_PPI_APER] = num_aper_pages;
	p2p->ppi_peer_id = peerdev->node;
	return p2p;
dma_unmap:
	dma_unmap_sg(&sdev->dev, p2p->ppi_sg[SCIF_PPI_MMIO],
		     p2p->sg_nentries[SCIF_PPI_MMIO], DMA_BIDIRECTIONAL);
scif_p2p_free:
	scif_p2p_freesg(p2p->ppi_sg[SCIF_PPI_MMIO]);
	scif_p2p_freesg(p2p->ppi_sg[SCIF_PPI_APER]);
free_p2p:
	kfree(p2p);
	return NULL;
}

/**
 * scif_node_connect: Respond to SCIF_NODE_CONNECT interrupt message
 * @dst: Destination node
 *
 * Connect the src and dst node by setting up the p2p connection
 * between them. Management node here acts like a proxy.
 */
static void scif_node_connect(struct scif_dev *scifdev, int dst)
{
	struct scif_dev *dev_j = scifdev;
	struct scif_dev *dev_i = NULL;
	struct scif_p2p_info *p2p_ij = NULL;    /* bus addr for j from i */
	struct scif_p2p_info *p2p_ji = NULL;    /* bus addr for i from j */
	struct scif_p2p_info *p2p;
	struct list_head *pos, *tmp;
	struct scifmsg msg;
	int err;
	u64 tmppayload;

	if (dst < 1 || dst > scif_info.maxid)
		return;

	dev_i = &scif_dev[dst];

	if (!_scifdev_alive(dev_i))
		return;
	/*
	 * If the p2p connection is already setup or in the process of setting
	 * up then just ignore this request. The requested node will get
	 * informed by SCIF_NODE_ADD_ACK or SCIF_NODE_ADD_NACK
	 */
	if (!list_empty(&dev_i->p2p)) {
		list_for_each_safe(pos, tmp, &dev_i->p2p) {
			p2p = list_entry(pos, struct scif_p2p_info, ppi_list);
			if (p2p->ppi_peer_id == dev_j->node)
				return;
		}
	}
	p2p_ij = scif_init_p2p_info(dev_i, dev_j);
	if (!p2p_ij)
		return;
	p2p_ji = scif_init_p2p_info(dev_j, dev_i);
	if (!p2p_ji)
		return;
	list_add_tail(&p2p_ij->ppi_list, &dev_i->p2p);
	list_add_tail(&p2p_ji->ppi_list, &dev_j->p2p);

	/*
	 * Send a SCIF_NODE_ADD to dev_i, pass it its bus address
	 * as seen from dev_j
	 */
	msg.uop = SCIF_NODE_ADD;
	msg.src.node = dev_j->node;
	msg.dst.node = dev_i->node;

	msg.payload[0] = p2p_ji->ppi_da[SCIF_PPI_APER];
	msg.payload[1] = p2p_ij->ppi_da[SCIF_PPI_MMIO];
	msg.payload[2] = p2p_ij->ppi_da[SCIF_PPI_APER];
	msg.payload[3] = p2p_ij->ppi_len[SCIF_PPI_APER] << PAGE_SHIFT;

	err = scif_nodeqp_send(dev_i,  &msg);
	if (err) {
		dev_err(&scifdev->sdev->dev,
			"%s %d error %d\n", __func__, __LINE__, err);
		return;
	}

	/* Same as above but to dev_j */
	msg.uop = SCIF_NODE_ADD;
	msg.src.node = dev_i->node;
	msg.dst.node = dev_j->node;

	tmppayload = msg.payload[0];
	msg.payload[0] = msg.payload[2];
	msg.payload[2] = tmppayload;
	msg.payload[1] = p2p_ji->ppi_da[SCIF_PPI_MMIO];
	msg.payload[3] = p2p_ji->ppi_len[SCIF_PPI_APER] << PAGE_SHIFT;

	scif_nodeqp_send(dev_j, &msg);
}

static void scif_p2p_setup(void)
{
	int i, j;

	if (!scif_info.p2p_enable)
		return;

	for (i = 1; i <= scif_info.maxid; i++)
		if (!_scifdev_alive(&scif_dev[i]))
			return;

	for (i = 1; i <= scif_info.maxid; i++) {
		for (j = 1; j <= scif_info.maxid; j++) {
			struct scif_dev *scifdev = &scif_dev[i];

			if (i == j)
				continue;
			scif_node_connect(scifdev, j);
		}
	}
}

void scif_qp_response_ack(struct work_struct *work)
{
	struct scif_dev *scifdev = container_of(work, struct scif_dev,
						init_msg_work);
	struct scif_peer_dev *spdev;

	/* Drop the INIT message if it has already been received */
	if (_scifdev_alive(scifdev))
		return;

	spdev = scif_peer_register_device(scifdev);
	if (IS_ERR(spdev))
		return;

	if (scif_is_mgmt_node()) {
		mutex_lock(&scif_info.conflock);
		scif_p2p_setup();
		mutex_unlock(&scif_info.conflock);
	}
}

static char *message_types[] = {"BAD",
				"INIT",
				"EXIT",
				"SCIF_EXIT_ACK",
				"SCIF_NODE_ADD",
				"SCIF_NODE_ADD_ACK",
				"SCIF_NODE_ADD_NACK",
				"REMOVE_NODE",
				"REMOVE_NODE_ACK",
				"CNCT_REQ",
				"CNCT_GNT",
				"CNCT_GNTACK",
				"CNCT_GNTNACK",
				"CNCT_REJ",
				"DISCNCT",
				"DISCNT_ACK",
				"CLIENT_SENT",
				"CLIENT_RCVD",
				"SCIF_GET_NODE_INFO"};

static void
scif_display_message(struct scif_dev *scifdev, struct scifmsg *msg,
		     const char *label)
{
	if (!scif_info.en_msg_log)
		return;
	if (msg->uop > SCIF_MAX_MSG) {
		dev_err(&scifdev->sdev->dev,
			"%s: unknown msg type %d\n", label, msg->uop);
		return;
	}
	dev_info(&scifdev->sdev->dev,
		 "%s: msg type %s, src %d:%d, dest %d:%d payload 0x%llx:0x%llx:0x%llx:0x%llx\n",
		 label, message_types[msg->uop], msg->src.node, msg->src.port,
		 msg->dst.node, msg->dst.port, msg->payload[0], msg->payload[1],
		 msg->payload[2], msg->payload[3]);
}

int _scif_nodeqp_send(struct scif_dev *scifdev, struct scifmsg *msg)
{
	struct scif_qp *qp = scifdev->qpairs;
	int err = -ENOMEM, loop_cnt = 0;

	scif_display_message(scifdev, msg, "Sent");
	if (!qp) {
		err = -EINVAL;
		goto error;
	}
	spin_lock(&qp->send_lock);

	while ((err = scif_rb_write(&qp->outbound_q,
				    msg, sizeof(struct scifmsg)))) {
		mdelay(1);
#define SCIF_NODEQP_SEND_TO_MSEC (3 * 1000)
		if (loop_cnt++ > (SCIF_NODEQP_SEND_TO_MSEC)) {
			err = -ENODEV;
			break;
		}
	}
	if (!err)
		scif_rb_commit(&qp->outbound_q);
	spin_unlock(&qp->send_lock);
	if (!err) {
		if (scifdev_self(scifdev))
			/*
			 * For loopback we need to emulate an interrupt by
			 * queuing work for the queue handling real node
			 * Qp interrupts.
			 */
			queue_work(scifdev->intr_wq, &scifdev->intr_bh);
		else
			scif_send_msg_intr(scifdev);
	}
error:
	if (err)
		dev_dbg(&scifdev->sdev->dev,
			"%s %d error %d uop %d\n",
			 __func__, __LINE__, err, msg->uop);
	return err;
}

/**
 * scif_nodeqp_send - Send a message on the node queue pair
 * @scifdev: Scif Device.
 * @msg: The message to be sent.
 */
int scif_nodeqp_send(struct scif_dev *scifdev, struct scifmsg *msg)
{
	int err;
	struct device *spdev = NULL;

	if (msg->uop > SCIF_EXIT_ACK) {
		/* Dont send messages once the exit flow has begun */
		if (OP_IDLE != scifdev->exit)
			return -ENODEV;
		spdev = scif_get_peer_dev(scifdev);
		if (IS_ERR(spdev)) {
			err = PTR_ERR(spdev);
			return err;
		}
	}
	err = _scif_nodeqp_send(scifdev, msg);
	if (msg->uop > SCIF_EXIT_ACK)
		scif_put_peer_dev(spdev);
	return err;
}

/*
 * scif_misc_handler:
 *
 * Work queue handler for servicing miscellaneous SCIF tasks.
 * Examples include:
 * 1) Cleanup of zombie endpoints.
 */
void scif_misc_handler(struct work_struct *work)
{
	scif_cleanup_zombie_epd();
}

/**
 * scif_init() - Respond to SCIF_INIT interrupt message
 * @scifdev:    Remote SCIF device node
 * @msg:        Interrupt message
 */
static __always_inline void
scif_init(struct scif_dev *scifdev, struct scifmsg *msg)
{
	/*
	 * Allow the thread waiting for device page updates for the peer QP DMA
	 * address to complete initializing the inbound_q.
	 */
	flush_delayed_work(&scifdev->qp_dwork);
	/*
	 * Delegate the peer device registration to a workqueue, otherwise if
	 * SCIF client probe (called during peer device registration) calls
	 * scif_connect(..), it will block the message processing thread causing
	 * a deadlock.
	 */
	schedule_work(&scifdev->init_msg_work);
}

/**
 * scif_exit() - Respond to SCIF_EXIT interrupt message
 * @scifdev:    Remote SCIF device node
 * @msg:        Interrupt message
 *
 * This function stops the SCIF interface for the node which sent
 * the SCIF_EXIT message and starts waiting for that node to
 * resetup the queue pair again.
 */
static __always_inline void
scif_exit(struct scif_dev *scifdev, struct scifmsg *unused)
{
	scifdev->exit_ack_pending = true;
	if (scif_is_mgmt_node())
		scif_disconnect_node(scifdev->node, false);
	else
		scif_stop(scifdev);
	schedule_delayed_work(&scifdev->qp_dwork,
			      msecs_to_jiffies(1000));
}

/**
 * scif_exitack() - Respond to SCIF_EXIT_ACK interrupt message
 * @scifdev:    Remote SCIF device node
 * @msg:        Interrupt message
 *
 */
static __always_inline void
scif_exit_ack(struct scif_dev *scifdev, struct scifmsg *unused)
{
	scifdev->exit = OP_COMPLETED;
	wake_up(&scif_info.exitwq);
}

/**
 * scif_node_add() - Respond to SCIF_NODE_ADD interrupt message
 * @scifdev:    Remote SCIF device node
 * @msg:        Interrupt message
 *
 * When the mgmt node driver has finished initializing a MIC node queue pair it
 * marks the node as online. It then looks for all currently online MIC cards
 * and send a SCIF_NODE_ADD message to identify the ID of the new card for
 * peer to peer initialization
 *
 * The local node allocates its incoming queue and sends its address in the
 * SCIF_NODE_ADD_ACK message back to the mgmt node, the mgmt node "reflects"
 * this message to the new node
 */
static __always_inline void
scif_node_add(struct scif_dev *scifdev, struct scifmsg *msg)
{
	struct scif_dev *newdev;
	dma_addr_t qp_offset;
	int qp_connect;
	struct scif_hw_dev *sdev;

	dev_dbg(&scifdev->sdev->dev,
		"Scifdev %d:%d received NODE_ADD msg for node %d\n",
		scifdev->node, msg->dst.node, msg->src.node);
	dev_dbg(&scifdev->sdev->dev,
		"Remote address for this node's aperture %llx\n",
		msg->payload[0]);
	newdev = &scif_dev[msg->src.node];
	newdev->node = msg->src.node;
	newdev->sdev = scif_dev[SCIF_MGMT_NODE].sdev;
	sdev = newdev->sdev;

	if (scif_setup_intr_wq(newdev)) {
		dev_err(&scifdev->sdev->dev,
			"failed to setup interrupts for %d\n", msg->src.node);
		goto interrupt_setup_error;
	}
	newdev->mmio.va = ioremap_nocache(msg->payload[1], sdev->mmio->len);
	if (!newdev->mmio.va) {
		dev_err(&scifdev->sdev->dev,
			"failed to map mmio for %d\n", msg->src.node);
		goto mmio_map_error;
	}
	newdev->qpairs = kzalloc(sizeof(*newdev->qpairs), GFP_KERNEL);
	if (!newdev->qpairs)
		goto qp_alloc_error;
	/*
	 * Set the base address of the remote node's memory since it gets
	 * added to qp_offset
	 */
	newdev->base_addr = msg->payload[0];

	qp_connect = scif_setup_qp_connect(newdev->qpairs, &qp_offset,
					   SCIF_NODE_QP_SIZE, newdev);
	if (qp_connect) {
		dev_err(&scifdev->sdev->dev,
			"failed to setup qp_connect %d\n", qp_connect);
		goto qp_connect_error;
	}

	newdev->db = sdev->hw_ops->next_db(sdev);
	newdev->cookie = sdev->hw_ops->request_irq(sdev, scif_intr_handler,
						   "SCIF_INTR", newdev,
						   newdev->db);
	if (IS_ERR(newdev->cookie))
		goto qp_connect_error;
	newdev->qpairs->magic = SCIFEP_MAGIC;
	newdev->qpairs->qp_state = SCIF_QP_OFFLINE;

	msg->uop = SCIF_NODE_ADD_ACK;
	msg->dst.node = msg->src.node;
	msg->src.node = scif_info.nodeid;
	msg->payload[0] = qp_offset;
	msg->payload[2] = newdev->db;
	scif_nodeqp_send(&scif_dev[SCIF_MGMT_NODE], msg);
	return;
qp_connect_error:
	kfree(newdev->qpairs);
	newdev->qpairs = NULL;
qp_alloc_error:
	iounmap(newdev->mmio.va);
	newdev->mmio.va = NULL;
mmio_map_error:
interrupt_setup_error:
	dev_err(&scifdev->sdev->dev,
		"node add failed for node %d\n", msg->src.node);
	msg->uop = SCIF_NODE_ADD_NACK;
	msg->dst.node = msg->src.node;
	msg->src.node = scif_info.nodeid;
	scif_nodeqp_send(&scif_dev[SCIF_MGMT_NODE], msg);
}

void scif_poll_qp_state(struct work_struct *work)
{
#define SCIF_NODE_QP_RETRY 100
#define SCIF_NODE_QP_TIMEOUT 100
	struct scif_dev *peerdev = container_of(work, struct scif_dev,
							p2p_dwork.work);
	struct scif_qp *qp = &peerdev->qpairs[0];

	if (qp->qp_state != SCIF_QP_ONLINE ||
	    qp->remote_qp->qp_state != SCIF_QP_ONLINE) {
		if (peerdev->p2p_retry++ == SCIF_NODE_QP_RETRY) {
			dev_err(&peerdev->sdev->dev,
				"Warning: QP check timeout with state %d\n",
				qp->qp_state);
			goto timeout;
		}
		schedule_delayed_work(&peerdev->p2p_dwork,
				      msecs_to_jiffies(SCIF_NODE_QP_TIMEOUT));
		return;
	}
	scif_peer_register_device(peerdev);
	return;
timeout:
	dev_err(&peerdev->sdev->dev,
		"%s %d remote node %d offline,  state = 0x%x\n",
		__func__, __LINE__, peerdev->node, qp->qp_state);
	qp->remote_qp->qp_state = SCIF_QP_OFFLINE;
	scif_cleanup_scifdev(peerdev);
}

/**
 * scif_node_add_ack() - Respond to SCIF_NODE_ADD_ACK interrupt message
 * @scifdev:    Remote SCIF device node
 * @msg:        Interrupt message
 *
 * After a MIC node receives the SCIF_NODE_ADD_ACK message it send this
 * message to the mgmt node to confirm the sequence is finished.
 *
 */
static __always_inline void
scif_node_add_ack(struct scif_dev *scifdev, struct scifmsg *msg)
{
	struct scif_dev *peerdev;
	struct scif_qp *qp;
	struct scif_dev *dst_dev = &scif_dev[msg->dst.node];

	dev_dbg(&scifdev->sdev->dev,
		"Scifdev %d received SCIF_NODE_ADD_ACK msg src %d dst %d\n",
		scifdev->node, msg->src.node, msg->dst.node);
	dev_dbg(&scifdev->sdev->dev,
		"payload %llx %llx %llx %llx\n", msg->payload[0],
		msg->payload[1], msg->payload[2], msg->payload[3]);
	if (scif_is_mgmt_node()) {
		/*
		 * the lock serializes with scif_qp_response_ack. The mgmt node
		 * is forwarding the NODE_ADD_ACK message from src to dst we
		 * need to make sure that the dst has already received a
		 * NODE_ADD for src and setup its end of the qp to dst
		 */
		mutex_lock(&scif_info.conflock);
		msg->payload[1] = scif_info.maxid;
		scif_nodeqp_send(dst_dev, msg);
		mutex_unlock(&scif_info.conflock);
		return;
	}
	peerdev = &scif_dev[msg->src.node];
	peerdev->sdev = scif_dev[SCIF_MGMT_NODE].sdev;
	peerdev->node = msg->src.node;

	qp = &peerdev->qpairs[0];

	if ((scif_setup_qp_connect_response(peerdev, &peerdev->qpairs[0],
					    msg->payload[0])))
		goto local_error;
	peerdev->rdb = msg->payload[2];
	qp->remote_qp->qp_state = SCIF_QP_ONLINE;
	schedule_delayed_work(&peerdev->p2p_dwork, 0);
	return;
local_error:
	scif_cleanup_scifdev(peerdev);
}

/**
 * scif_node_add_nack: Respond to SCIF_NODE_ADD_NACK interrupt message
 * @msg:        Interrupt message
 *
 * SCIF_NODE_ADD failed, so inform the waiting wq.
 */
static __always_inline void
scif_node_add_nack(struct scif_dev *scifdev, struct scifmsg *msg)
{
	if (scif_is_mgmt_node()) {
		struct scif_dev *dst_dev = &scif_dev[msg->dst.node];

		dev_dbg(&scifdev->sdev->dev,
			"SCIF_NODE_ADD_NACK received from %d\n", scifdev->node);
		scif_nodeqp_send(dst_dev, msg);
	}
}

/*
 * scif_node_remove: Handle SCIF_NODE_REMOVE message
 * @msg: Interrupt message
 *
 * Handle node removal.
 */
static __always_inline void
scif_node_remove(struct scif_dev *scifdev, struct scifmsg *msg)
{
	int node = msg->payload[0];
	struct scif_dev *scdev = &scif_dev[node];

	scdev->node_remove_ack_pending = true;
	scif_handle_remove_node(node);
}

/*
 * scif_node_remove_ack: Handle SCIF_NODE_REMOVE_ACK message
 * @msg: Interrupt message
 *
 * The peer has acked a SCIF_NODE_REMOVE message.
 */
static __always_inline void
scif_node_remove_ack(struct scif_dev *scifdev, struct scifmsg *msg)
{
	struct scif_dev *sdev = &scif_dev[msg->payload[0]];

	atomic_inc(&sdev->disconn_rescnt);
	wake_up(&sdev->disconn_wq);
}

/**
 * scif_get_node_info: Respond to SCIF_GET_NODE_INFO interrupt message
 * @msg:        Interrupt message
 *
 * Retrieve node info i.e maxid and total from the mgmt node.
 */
static __always_inline void
scif_get_node_info_resp(struct scif_dev *scifdev, struct scifmsg *msg)
{
	if (scif_is_mgmt_node()) {
		swap(msg->dst.node, msg->src.node);
		mutex_lock(&scif_info.conflock);
		msg->payload[1] = scif_info.maxid;
		msg->payload[2] = scif_info.total;
		mutex_unlock(&scif_info.conflock);
		scif_nodeqp_send(scifdev, msg);
	} else {
		struct completion *node_info =
			(struct completion *)msg->payload[3];

		mutex_lock(&scif_info.conflock);
		scif_info.maxid = msg->payload[1];
		scif_info.total = msg->payload[2];
		complete_all(node_info);
		mutex_unlock(&scif_info.conflock);
	}
}

static void
scif_msg_unknown(struct scif_dev *scifdev, struct scifmsg *msg)
{
	/* Bogus Node Qp Message? */
	dev_err(&scifdev->sdev->dev,
		"Unknown message 0x%xn scifdev->node 0x%x\n",
		msg->uop, scifdev->node);
}

static void (*scif_intr_func[SCIF_MAX_MSG + 1])
	    (struct scif_dev *, struct scifmsg *msg) = {
	scif_msg_unknown,	/* Error */
	scif_init,		/* SCIF_INIT */
	scif_exit,		/* SCIF_EXIT */
	scif_exit_ack,		/* SCIF_EXIT_ACK */
	scif_node_add,		/* SCIF_NODE_ADD */
	scif_node_add_ack,	/* SCIF_NODE_ADD_ACK */
	scif_node_add_nack,	/* SCIF_NODE_ADD_NACK */
	scif_node_remove,	/* SCIF_NODE_REMOVE */
	scif_node_remove_ack,	/* SCIF_NODE_REMOVE_ACK */
	scif_cnctreq,		/* SCIF_CNCT_REQ */
	scif_cnctgnt,		/* SCIF_CNCT_GNT */
	scif_cnctgnt_ack,	/* SCIF_CNCT_GNTACK */
	scif_cnctgnt_nack,	/* SCIF_CNCT_GNTNACK */
	scif_cnctrej,		/* SCIF_CNCT_REJ */
	scif_discnct,		/* SCIF_DISCNCT */
	scif_discnt_ack,	/* SCIF_DISCNT_ACK */
	scif_clientsend,	/* SCIF_CLIENT_SENT */
	scif_clientrcvd,	/* SCIF_CLIENT_RCVD */
	scif_get_node_info_resp,/* SCIF_GET_NODE_INFO */
};

/**
 * scif_nodeqp_msg_handler() - Common handler for node messages
 * @scifdev: Remote device to respond to
 * @qp: Remote memory pointer
 * @msg: The message to be handled.
 *
 * This routine calls the appropriate routine to handle a Node Qp
 * message receipt
 */
static int scif_max_msg_id = SCIF_MAX_MSG;

static void
scif_nodeqp_msg_handler(struct scif_dev *scifdev,
			struct scif_qp *qp, struct scifmsg *msg)
{
	scif_display_message(scifdev, msg, "Rcvd");

	if (msg->uop > (u32)scif_max_msg_id) {
		/* Bogus Node Qp Message? */
		dev_err(&scifdev->sdev->dev,
			"Unknown message 0x%xn scifdev->node 0x%x\n",
			msg->uop, scifdev->node);
		return;
	}

	scif_intr_func[msg->uop](scifdev, msg);
}

/**
 * scif_nodeqp_intrhandler() - Interrupt handler for node messages
 * @scifdev:    Remote device to respond to
 * @qp:         Remote memory pointer
 *
 * This routine is triggered by the interrupt mechanism.  It reads
 * messages from the node queue RB and calls the Node QP Message handling
 * routine.
 */
void scif_nodeqp_intrhandler(struct scif_dev *scifdev, struct scif_qp *qp)
{
	struct scifmsg msg;
	int read_size;

	do {
		read_size = scif_rb_get_next(&qp->inbound_q, &msg, sizeof(msg));
		if (!read_size)
			break;
		scif_nodeqp_msg_handler(scifdev, qp, &msg);
		/*
		 * The node queue pair is unmapped so skip the read pointer
		 * update after receipt of a SCIF_EXIT_ACK
		 */
		if (SCIF_EXIT_ACK == msg.uop)
			break;
		scif_rb_update_read_ptr(&qp->inbound_q);
	} while (1);
}

/**
 * scif_loopb_wq_handler - Loopback Workqueue Handler.
 * @work: loop back work
 *
 * This work queue routine is invoked by the loopback work queue handler.
 * It grabs the recv lock, dequeues any available messages from the head
 * of the loopback message list, calls the node QP message handler,
 * waits for it to return, then frees up this message and dequeues more
 * elements of the list if available.
 */
static void scif_loopb_wq_handler(struct work_struct *unused)
{
	struct scif_dev *scifdev = scif_info.loopb_dev;
	struct scif_qp *qp = scifdev->qpairs;
	struct scif_loopb_msg *msg;

	do {
		msg = NULL;
		spin_lock(&qp->recv_lock);
		if (!list_empty(&scif_info.loopb_recv_q)) {
			msg = list_first_entry(&scif_info.loopb_recv_q,
					       struct scif_loopb_msg,
					       list);
			list_del(&msg->list);
		}
		spin_unlock(&qp->recv_lock);

		if (msg) {
			scif_nodeqp_msg_handler(scifdev, qp, &msg->msg);
			kfree(msg);
		}
	} while (msg);
}

/**
 * scif_loopb_msg_handler() - Workqueue handler for loopback messages.
 * @scifdev: SCIF device
 * @qp: Queue pair.
 *
 * This work queue routine is triggered when a loopback message is received.
 *
 * We need special handling for receiving Node Qp messages on a loopback SCIF
 * device via two workqueues for receiving messages.
 *
 * The reason we need the extra workqueue which is not required with *normal*
 * non-loopback SCIF devices is the potential classic deadlock described below:
 *
 * Thread A tries to send a message on a loopback SCIF device and blocks since
 * there is no space in the RB while it has the send_lock held or another
 * lock called lock X for example.
 *
 * Thread B: The Loopback Node QP message receive workqueue receives the message
 * and tries to send a message (eg an ACK) to the loopback SCIF device. It tries
 * to grab the send lock again or lock X and deadlocks with Thread A. The RB
 * cannot be drained any further due to this classic deadlock.
 *
 * In order to avoid deadlocks as mentioned above we have an extra level of
 * indirection achieved by having two workqueues.
 * 1) The first workqueue whose handler is scif_loopb_msg_handler reads
 * messages from the Node QP RB, adds them to a list and queues work for the
 * second workqueue.
 *
 * 2) The second workqueue whose handler is scif_loopb_wq_handler dequeues
 * messages from the list, handles them, frees up the memory and dequeues
 * more elements from the list if possible.
 */
int
scif_loopb_msg_handler(struct scif_dev *scifdev, struct scif_qp *qp)
{
	int read_size;
	struct scif_loopb_msg *msg;

	do {
		msg = kmalloc(sizeof(*msg), GFP_KERNEL);
		if (!msg)
			return -ENOMEM;
		read_size = scif_rb_get_next(&qp->inbound_q, &msg->msg,
					     sizeof(struct scifmsg));
		if (read_size != sizeof(struct scifmsg)) {
			kfree(msg);
			scif_rb_update_read_ptr(&qp->inbound_q);
			break;
		}
		spin_lock(&qp->recv_lock);
		list_add_tail(&msg->list, &scif_info.loopb_recv_q);
		spin_unlock(&qp->recv_lock);
		queue_work(scif_info.loopb_wq, &scif_info.loopb_work);
		scif_rb_update_read_ptr(&qp->inbound_q);
	} while (read_size == sizeof(struct scifmsg));
	return read_size;
}

/**
 * scif_setup_loopback_qp - One time setup work for Loopback Node Qp.
 * @scifdev: SCIF device
 *
 * Sets up the required loopback workqueues, queue pairs and ring buffers
 */
int scif_setup_loopback_qp(struct scif_dev *scifdev)
{
	int err = 0;
	void *local_q;
	struct scif_qp *qp;
	struct scif_peer_dev *spdev;

	err = scif_setup_intr_wq(scifdev);
	if (err)
		goto exit;
	INIT_LIST_HEAD(&scif_info.loopb_recv_q);
	snprintf(scif_info.loopb_wqname, sizeof(scif_info.loopb_wqname),
		 "SCIF LOOPB %d", scifdev->node);
	scif_info.loopb_wq =
		alloc_ordered_workqueue(scif_info.loopb_wqname, 0);
	if (!scif_info.loopb_wq) {
		err = -ENOMEM;
		goto destroy_intr;
	}
	INIT_WORK(&scif_info.loopb_work, scif_loopb_wq_handler);
	/* Allocate Self Qpair */
	scifdev->qpairs = kzalloc(sizeof(*scifdev->qpairs), GFP_KERNEL);
	if (!scifdev->qpairs) {
		err = -ENOMEM;
		goto destroy_loopb_wq;
	}

	qp = scifdev->qpairs;
	qp->magic = SCIFEP_MAGIC;
	spin_lock_init(&qp->send_lock);
	spin_lock_init(&qp->recv_lock);

	local_q = kzalloc(SCIF_NODE_QP_SIZE, GFP_KERNEL);
	if (!local_q) {
		err = -ENOMEM;
		goto free_qpairs;
	}
	/*
	 * For loopback the inbound_q and outbound_q are essentially the same
	 * since the Node sends a message on the loopback interface to the
	 * outbound_q which is then received on the inbound_q.
	 */
	scif_rb_init(&qp->outbound_q,
		     &qp->local_read,
		     &qp->local_write,
		     local_q, get_count_order(SCIF_NODE_QP_SIZE));

	scif_rb_init(&qp->inbound_q,
		     &qp->local_read,
		     &qp->local_write,
		     local_q, get_count_order(SCIF_NODE_QP_SIZE));
	scif_info.nodeid = scifdev->node;
	spdev = scif_peer_register_device(scifdev);
	if (IS_ERR(spdev)) {
		err = PTR_ERR(spdev);
		goto free_local_q;
	}
	scif_info.loopb_dev = scifdev;
	return err;
free_local_q:
	kfree(local_q);
free_qpairs:
	kfree(scifdev->qpairs);
destroy_loopb_wq:
	destroy_workqueue(scif_info.loopb_wq);
destroy_intr:
	scif_destroy_intr_wq(scifdev);
exit:
	return err;
}

/**
 * scif_destroy_loopback_qp - One time uninit work for Loopback Node Qp
 * @scifdev: SCIF device
 *
 * Destroys the workqueues and frees up the Ring Buffer and Queue Pair memory.
 */
int scif_destroy_loopback_qp(struct scif_dev *scifdev)
{
	struct scif_peer_dev *spdev;

	rcu_read_lock();
	spdev = rcu_dereference(scifdev->spdev);
	rcu_read_unlock();
	if (spdev)
		scif_peer_unregister_device(spdev);
	destroy_workqueue(scif_info.loopb_wq);
	scif_destroy_intr_wq(scifdev);
	kfree(scifdev->qpairs->outbound_q.rb_base);
	kfree(scifdev->qpairs);
	scifdev->sdev = NULL;
	scif_info.loopb_dev = NULL;
	return 0;
}

void scif_destroy_p2p(struct scif_dev *scifdev)
{
	struct scif_dev *peer_dev;
	struct scif_p2p_info *p2p;
	struct list_head *pos, *tmp;
	int bd;

	mutex_lock(&scif_info.conflock);
	/* Free P2P mappings in the given node for all its peer nodes */
	list_for_each_safe(pos, tmp, &scifdev->p2p) {
		p2p = list_entry(pos, struct scif_p2p_info, ppi_list);
		dma_unmap_sg(&scifdev->sdev->dev, p2p->ppi_sg[SCIF_PPI_MMIO],
			     p2p->sg_nentries[SCIF_PPI_MMIO],
			     DMA_BIDIRECTIONAL);
		dma_unmap_sg(&scifdev->sdev->dev, p2p->ppi_sg[SCIF_PPI_APER],
			     p2p->sg_nentries[SCIF_PPI_APER],
			     DMA_BIDIRECTIONAL);
		scif_p2p_freesg(p2p->ppi_sg[SCIF_PPI_MMIO]);
		scif_p2p_freesg(p2p->ppi_sg[SCIF_PPI_APER]);
		list_del(pos);
		kfree(p2p);
	}

	/* Free P2P mapping created in the peer nodes for the given node */
	for (bd = SCIF_MGMT_NODE + 1; bd <= scif_info.maxid; bd++) {
		peer_dev = &scif_dev[bd];
		list_for_each_safe(pos, tmp, &peer_dev->p2p) {
			p2p = list_entry(pos, struct scif_p2p_info, ppi_list);
			if (p2p->ppi_peer_id == scifdev->node) {
				dma_unmap_sg(&peer_dev->sdev->dev,
					     p2p->ppi_sg[SCIF_PPI_MMIO],
					     p2p->sg_nentries[SCIF_PPI_MMIO],
					     DMA_BIDIRECTIONAL);
				dma_unmap_sg(&peer_dev->sdev->dev,
					     p2p->ppi_sg[SCIF_PPI_APER],
					     p2p->sg_nentries[SCIF_PPI_APER],
					     DMA_BIDIRECTIONAL);
				scif_p2p_freesg(p2p->ppi_sg[SCIF_PPI_MMIO]);
				scif_p2p_freesg(p2p->ppi_sg[SCIF_PPI_APER]);
				list_del(pos);
				kfree(p2p);
			}
		}
	}
	mutex_unlock(&scif_info.conflock);
}
