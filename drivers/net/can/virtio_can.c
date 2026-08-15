// SPDX-License-Identifier: GPL-2.0-only
/*
 * CAN bus driver for the Virtio CAN controller
 *
 * Copyright (C) 2021-2023 OpenSynergy GmbH
 * Copyright Red Hat, Inc. 2025
 */

#include <linux/atomic.h>
#include <linux/idr.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/netdevice.h>
#include <linux/stddef.h>
#include <linux/can/dev.h>
#include <linux/virtio.h>
#include <linux/virtio_ring.h>
#include <linux/virtio_can.h>

/* CAN device queues */
#define VIRTIO_CAN_QUEUE_TX 0
#define VIRTIO_CAN_QUEUE_RX 1
#define VIRTIO_CAN_QUEUE_CONTROL 2
#define VIRTIO_CAN_QUEUE_COUNT 3

#define CAN_KNOWN_FLAGS \
	(VIRTIO_CAN_FLAGS_EXTENDED |\
	 VIRTIO_CAN_FLAGS_FD |\
	 VIRTIO_CAN_FLAGS_RTR)

/* Max. number of in flight TX messages */
#define VIRTIO_CAN_ECHO_SKB_MAX 128

struct virtio_can_tx {
	unsigned int putidx;
	struct virtio_can_tx_in tx_in;
	/* Keep virtio_can_tx_out at the end of the structure due to flex array */
	struct virtio_can_tx_out tx_out;
};

struct virtio_can_control {
	struct virtio_can_control_out cpkt_out;
	struct virtio_can_control_in cpkt_in;
};

/* virtio_can private data structure */
struct virtio_can_priv {
	struct can_priv can;	/* must be the first member */
	/* NAPI for RX messages */
	struct napi_struct napi;
	/* NAPI for TX messages */
	struct napi_struct napi_tx;
	/* The network device we're associated with */
	struct net_device *dev;
	/* The virtio device we're associated with */
	struct virtio_device *vdev;
	/* The virtqueues */
	struct virtqueue *vqs[VIRTIO_CAN_QUEUE_COUNT];
	/* Lock for TX operations */
	spinlock_t tx_lock;
	/* Control queue lock */
	struct mutex ctrl_lock;
	/* Wait for control queue processing without polling */
	struct completion ctrl_done;
	/* Array of receive queue messages */
	struct virtio_can_rx *rpkt;
	struct virtio_can_control can_ctr_msg;
	/* Data to get and maintain the putidx for local TX echo */
	struct ida tx_putidx_ida;
	/* In flight TX messages */
	atomic_t tx_inflight;
	/* Packet length */
	int rpkt_len;
	/* BusOff pending. Reset after successful indication to upper layer */
	bool busoff_pending;
	/* Tracks whether NAPI instances are currently enabled */
	bool napi_active;
};

static void virtqueue_napi_schedule(struct napi_struct *napi,
				    struct virtqueue *vq)
{
	if (napi_schedule_prep(napi)) {
		virtqueue_disable_cb(vq);
		__napi_schedule(napi);
	}
}

static void virtqueue_napi_complete(struct napi_struct *napi,
				    struct virtqueue *vq, int processed)
{
	int opaque;

	opaque = virtqueue_enable_cb_prepare(vq);
	if (napi_complete_done(napi, processed)) {
		if (unlikely(virtqueue_poll(vq, opaque)))
			virtqueue_napi_schedule(napi, vq);
	} else {
		virtqueue_disable_cb(vq);
	}
}

static void virtio_can_free_candev(struct net_device *ndev)
{
	struct virtio_can_priv *priv = netdev_priv(ndev);

	ida_destroy(&priv->tx_putidx_ida);
	free_candev(ndev);
}

static void virtio_can_napi_enable(struct virtio_can_priv *priv)
{
	if (!priv->napi_active) {
		napi_enable(&priv->napi);
		napi_enable(&priv->napi_tx);
		priv->napi_active = true;
	}
}

static void virtio_can_napi_disable(struct virtio_can_priv *priv)
{
	if (priv->napi_active) {
		napi_disable(&priv->napi_tx);
		napi_disable(&priv->napi);
		priv->napi_active = false;
	}
}

static int virtio_can_alloc_tx_idx(struct virtio_can_priv *priv)
{
	int tx_idx;

	tx_idx = ida_alloc_max(&priv->tx_putidx_ida,
			       priv->can.echo_skb_max - 1, GFP_ATOMIC);
	if (tx_idx >= 0)
		atomic_inc(&priv->tx_inflight);

	return tx_idx;
}

static void virtio_can_free_tx_idx(struct virtio_can_priv *priv,
				   unsigned int idx)
{
	ida_free(&priv->tx_putidx_ida, idx);
	atomic_dec(&priv->tx_inflight);
}

/* Create a scatter-gather list representing our input buffer and put
 * it in the queue.
 *
 * Callers should take appropriate locks.
 */
static int virtio_can_add_inbuf(struct virtqueue *vq, void *buf,
				unsigned int size)
{
	struct scatterlist sg[1];
	int ret;

	sg_init_one(sg, buf, size);

	ret = virtqueue_add_inbuf(vq, sg, 1, buf, GFP_ATOMIC);

	return ret;
}

/* Send a control message with message type either
 *
 * - VIRTIO_CAN_SET_CTRL_MODE_START or
 * - VIRTIO_CAN_SET_CTRL_MODE_STOP.
 *
 */
static u8 virtio_can_send_ctrl_msg(struct net_device *ndev, u16 msg_type)
{
	struct scatterlist sg_out, sg_in, *sgs[2] = { &sg_out, &sg_in };
	struct virtio_can_priv *priv = netdev_priv(ndev);
	struct virtqueue *vq = priv->vqs[VIRTIO_CAN_QUEUE_CONTROL];
	struct device *dev = &priv->vdev->dev;
	unsigned int len;
	int err;

	if (!vq)
		return VIRTIO_CAN_RESULT_NOT_OK;

	guard(mutex)(&priv->ctrl_lock);

	priv->can_ctr_msg.cpkt_out.msg_type = cpu_to_le16(msg_type);
	sg_init_one(&sg_out, &priv->can_ctr_msg.cpkt_out,
		    sizeof(priv->can_ctr_msg.cpkt_out));
	sg_init_one(&sg_in, &priv->can_ctr_msg.cpkt_in, sizeof(priv->can_ctr_msg.cpkt_in));

	reinit_completion(&priv->ctrl_done);

	err = virtqueue_add_sgs(vq, sgs, 1u, 1u, priv, GFP_ATOMIC);
	if (err != 0) {
		dev_err(dev, "%s(): virtqueue_add_sgs() failed\n", __func__);
		return VIRTIO_CAN_RESULT_NOT_OK;
	}

	if (!virtqueue_kick(vq)) {
		dev_err(dev, "%s(): Kick failed\n", __func__);
		return VIRTIO_CAN_RESULT_NOT_OK;
	}

	while (!virtqueue_get_buf(vq, &len) && !virtqueue_is_broken(vq))
		wait_for_completion(&priv->ctrl_done);

	return priv->can_ctr_msg.cpkt_in.result;
}

static int virtio_can_start(struct net_device *ndev)
{
	struct virtio_can_priv *priv = netdev_priv(ndev);
	u8 result;

	result = virtio_can_send_ctrl_msg(ndev, VIRTIO_CAN_SET_CTRL_MODE_START);
	if (result != VIRTIO_CAN_RESULT_OK) {
		netdev_err(ndev, "CAN controller start failed\n");
		return -EIO;
	}

	priv->busoff_pending = false;
	priv->can.state = CAN_STATE_ERROR_ACTIVE;

	return 0;
}

static int virtio_can_set_mode(struct net_device *dev, enum can_mode mode)
{
	int err;

	switch (mode) {
	case CAN_MODE_START:
		err = virtio_can_start(dev);
		if (err)
			return err;
		netif_wake_queue(dev);
		break;
	default:
		return -EOPNOTSUPP;
	}

	return 0;
}

static int virtio_can_open(struct net_device *ndev)
{
	struct virtio_can_priv *priv = netdev_priv(ndev);
	int err;

	err = open_candev(ndev);
	if (err)
		return err;

	err = virtio_can_start(ndev);
	if (err) {
		close_candev(ndev);
		return err;
	}

	virtio_can_napi_enable(priv);
	netif_start_queue(ndev);

	return 0;
}

static int virtio_can_stop(struct net_device *ndev)
{
	struct virtio_can_priv *priv = netdev_priv(ndev);
	struct device *dev = &priv->vdev->dev;
	u8 result;

	result = virtio_can_send_ctrl_msg(ndev, VIRTIO_CAN_SET_CTRL_MODE_STOP);
	if (result != VIRTIO_CAN_RESULT_OK) {
		dev_err(dev, "CAN controller stop failed\n");
		return -EIO;
	}

	priv->busoff_pending = false;
	priv->can.state = CAN_STATE_STOPPED;

	/* Switch carrier off if device was connected to the bus */
	if (netif_carrier_ok(ndev))
		netif_carrier_off(ndev);

	return 0;
}

static int virtio_can_close(struct net_device *dev)
{
	struct virtio_can_priv *priv = netdev_priv(dev);

	netif_stop_queue(dev);
	/* Ignore stop error: ndo_stop must always complete cleanup regardless.
	 * virtio_can_stop() already logs the error if it fails.
	 */
	virtio_can_stop(dev);
	virtio_can_napi_disable(priv);
	close_candev(dev);

	return 0;
}

static netdev_tx_t virtio_can_start_xmit(struct sk_buff *skb,
					 struct net_device *dev)
{
	struct scatterlist sg_out, sg_in, *sgs[2] = { &sg_out, &sg_in };
	const unsigned int hdr_size = sizeof(struct virtio_can_tx_out);
	struct canfd_frame *cf = (struct canfd_frame *)skb->data;
	struct virtio_can_priv *priv = netdev_priv(dev);
	struct virtqueue *vq = priv->vqs[VIRTIO_CAN_QUEUE_TX];
	netdev_tx_t xmit_ret = NETDEV_TX_OK;
	struct virtio_can_tx *can_tx_msg;
	u32 can_flags;
	int putidx;
	int err;

	if (can_dev_dropped_skb(dev, skb))
		goto kick; /* No way to return NET_XMIT_DROP here */

	/* No local check for CAN_RTR_FLAG or FD frame against negotiated
	 * features. The device will reject those anyway if not supported.
	 */

	can_tx_msg = kzalloc(sizeof(*can_tx_msg) + cf->len, GFP_ATOMIC);
	if (!can_tx_msg) {
		kfree_skb(skb);
		dev->stats.tx_dropped++;
		goto kick; /* No way to return NET_XMIT_DROP here */
	}

	can_tx_msg->tx_out.msg_type = cpu_to_le16(VIRTIO_CAN_TX);
	can_tx_msg->tx_out.length = cpu_to_le16(cf->len);
	can_flags = 0;

	if (cf->can_id & CAN_EFF_FLAG) {
		can_flags |= VIRTIO_CAN_FLAGS_EXTENDED;
		can_tx_msg->tx_out.can_id = cpu_to_le32(cf->can_id & CAN_EFF_MASK);
	} else {
		can_tx_msg->tx_out.can_id = cpu_to_le32(cf->can_id & CAN_SFF_MASK);
	}
	if (cf->can_id & CAN_RTR_FLAG)
		can_flags |= VIRTIO_CAN_FLAGS_RTR;
	else
		memcpy(can_tx_msg->tx_out.sdu, cf->data, cf->len);
	if (can_is_canfd_skb(skb))
		can_flags |= VIRTIO_CAN_FLAGS_FD;

	can_tx_msg->tx_out.flags = cpu_to_le32(can_flags);

	sg_init_one(&sg_out, &can_tx_msg->tx_out, hdr_size + cf->len);
	sg_init_one(&sg_in, &can_tx_msg->tx_in, sizeof(can_tx_msg->tx_in));

	putidx = virtio_can_alloc_tx_idx(priv);

	if (unlikely(putidx < 0)) {
		/* -ENOMEM or -ENOSPC here. -ENOSPC should not be possible as
		 * tx_inflight >= can.echo_skb_max is checked in flow control
		 */
		WARN_ON_ONCE(putidx == -ENOSPC);
		kfree(can_tx_msg);
		kfree_skb(skb);
		dev->stats.tx_dropped++;
		goto kick; /* No way to return NET_XMIT_DROP here */
	}

	can_tx_msg->putidx = (unsigned int)putidx;

	/* Push loopback echo. Will be looped back on TX interrupt/TX NAPI */
	err = can_put_echo_skb(skb, dev, can_tx_msg->putidx, 0);
	if (unlikely(err)) {
		/* skb was already freed by can_put_echo_skb() on error */
		virtio_can_free_tx_idx(priv, can_tx_msg->putidx);
		kfree(can_tx_msg);
		dev->stats.tx_dropped++;
		goto kick;
	}

	/* Protect queue and list operations */
	scoped_guard(spinlock_irqsave, &priv->tx_lock)
		err = virtqueue_add_sgs(vq, sgs, 1u, 1u, can_tx_msg, GFP_ATOMIC);

	if (unlikely(err)) {
		/*
		 * can_put_echo_skb() already consumed skb via consume_skb(),
		 * so returning NETDEV_TX_BUSY would cause the stack to requeue
		 * a freed pointer. Drop the frame and return OK instead.
		 */
		can_free_echo_skb(dev, can_tx_msg->putidx, NULL);
		virtio_can_free_tx_idx(priv, can_tx_msg->putidx);
		netif_stop_queue(dev);
		kfree(can_tx_msg);
		dev->stats.tx_dropped++;
		/* Expected never to be seen */
		netdev_warn(dev, "TX: Stop queue, err = %d\n", err);
		goto kick;
	}

	/* Normal flow control: stop queue when no transmission slots left */
	if (atomic_read(&priv->tx_inflight) >= priv->can.echo_skb_max ||
	    vq->num_free == 0 || (vq->num_free < ARRAY_SIZE(sgs) &&
	    !virtio_has_feature(vq->vdev, VIRTIO_RING_F_INDIRECT_DESC))) {
		netif_stop_queue(dev);
		netdev_dbg(dev, "TX: Normal stop queue\n");
	}

kick:
	if (netif_queue_stopped(dev) || !netdev_xmit_more()) {
		scoped_guard(spinlock_irqsave, &priv->tx_lock) {
			if (!virtqueue_kick(vq))
				netdev_err(dev, "%s(): Kick failed\n", __func__);
		}
	}

	return xmit_ret;
}

static const struct net_device_ops virtio_can_netdev_ops = {
	.ndo_open = virtio_can_open,
	.ndo_stop = virtio_can_close,
	.ndo_start_xmit = virtio_can_start_xmit,
};

static int register_virtio_can_dev(struct net_device *dev)
{
	dev->flags |= IFF_ECHO;	/* we support local echo */
	dev->netdev_ops = &virtio_can_netdev_ops;

	return register_candev(dev);
}

static int virtio_can_read_tx_queue(struct virtqueue *vq)
{
	struct virtio_can_priv *can_priv = vq->vdev->priv;
	struct net_device *dev = can_priv->dev;
	struct virtio_can_tx *can_tx_msg;
	struct net_device_stats *stats;
	unsigned int len;
	u8 result;

	stats = &dev->stats;

	scoped_guard(spinlock_irqsave, &can_priv->tx_lock)
		can_tx_msg = virtqueue_get_buf(vq, &len);

	if (!can_tx_msg)
		return 0;

	if (unlikely(len < sizeof(struct virtio_can_tx_in))) {
		netdev_err(dev, "TX ACK: Device sent no result code\n");
		result = VIRTIO_CAN_RESULT_NOT_OK; /* Keep things going */
	} else {
		result = can_tx_msg->tx_in.result;
	}

	if (can_priv->can.state < CAN_STATE_BUS_OFF) {
		if (result != VIRTIO_CAN_RESULT_OK) {
			struct can_frame *skb_cf;
			struct sk_buff *skb = alloc_can_err_skb(dev, &skb_cf);

			if (skb) {
				skb_cf->can_id |= CAN_ERR_CRTL;
				skb_cf->data[1] |= CAN_ERR_CRTL_UNSPEC;
				netif_rx(skb);
			}
			netdev_warn(dev, "TX ACK: Result = %u\n", result);
			can_free_echo_skb(dev, can_tx_msg->putidx, NULL);
			stats->tx_dropped++;
		} else {
			stats->tx_bytes += can_get_echo_skb(dev, can_tx_msg->putidx,
				NULL);
			stats->tx_packets++;
		}
	} else {
		netdev_dbg(dev, "TX ACK: Controller inactive, drop echo\n");
		can_free_echo_skb(dev, can_tx_msg->putidx, NULL);
		stats->tx_dropped++;
	}

	virtio_can_free_tx_idx(can_priv, can_tx_msg->putidx);

	/* Flow control */
	if (netif_queue_stopped(dev)) {
		netdev_dbg(dev, "TX ACK: Wake up stopped queue\n");
		netif_wake_queue(dev);
	}

	kfree(can_tx_msg);

	return 1; /* Queue was not empty so there may be more data */
}

static int virtio_can_tx_poll(struct napi_struct *napi, int quota)
{
	struct net_device *dev = napi->dev;
	struct virtio_can_priv *priv = netdev_priv(dev);
	struct virtqueue *vq = priv->vqs[VIRTIO_CAN_QUEUE_TX];
	int work_done = 0;

	while (work_done < quota && virtio_can_read_tx_queue(vq) != 0)
		work_done++;

	if (work_done < quota)
		virtqueue_napi_complete(napi, vq, work_done);

	return work_done;
}

static void virtio_can_tx_intr(struct virtqueue *vq)
{
	struct virtio_can_priv *can_priv = vq->vdev->priv;

	virtqueue_disable_cb(vq);
	napi_schedule(&can_priv->napi_tx);
}

/* This function is the NAPI RX poll function and NAPI guarantees that this
 * function is not invoked simultaneously on multiple processors.
 * Read a RX message from the used queue and sends it to the upper layer.
 */
static int virtio_can_read_rx_queue(struct virtqueue *vq)
{
	const unsigned int header_size = sizeof(struct virtio_can_rx);
	struct virtio_can_priv *priv = vq->vdev->priv;
	struct net_device *dev = priv->dev;
	struct net_device_stats *stats;
	struct virtio_can_rx *can_rx;
	unsigned int transport_len;
	struct canfd_frame *cf;
	struct sk_buff *skb;
	unsigned int len;
	u32 can_flags;
	u16 msg_type;
	u32 can_id;
	int ret;

	stats = &dev->stats;

	can_rx = virtqueue_get_buf(vq, &transport_len);
	if (!can_rx)
		return 0; /* No more data */

	if (transport_len < header_size) {
		netdev_warn(dev, "RX: Message too small\n");
		goto putback;
	}

	if (priv->can.state >= CAN_STATE_ERROR_PASSIVE) {
		netdev_dbg(dev, "%s(): Controller not active\n", __func__);
		goto putback;
	}

	msg_type = le16_to_cpu(can_rx->msg_type);
	if (msg_type != VIRTIO_CAN_RX) {
		netdev_warn(dev, "RX: Got unknown msg_type %04x\n", msg_type);
		goto putback;
	}

	len = le16_to_cpu(can_rx->length);
	can_flags = le32_to_cpu(can_rx->flags);
	can_id = le32_to_cpu(can_rx->can_id);

	if (can_flags & ~CAN_KNOWN_FLAGS) {
		stats->rx_dropped++;
		netdev_warn(dev, "RX: CAN Id 0x%08x: Invalid flags 0x%x\n",
			    can_id, can_flags);
		goto putback;
	}

	if (can_flags & VIRTIO_CAN_FLAGS_EXTENDED) {
		can_id &= CAN_EFF_MASK;
		can_id |= CAN_EFF_FLAG;
	} else {
		can_id &= CAN_SFF_MASK;
	}

	if (can_flags & VIRTIO_CAN_FLAGS_RTR) {
		if (!virtio_has_feature(vq->vdev, VIRTIO_CAN_F_RTR_FRAMES)) {
			stats->rx_dropped++;
			netdev_warn(dev, "RX: CAN Id 0x%08x: RTR not negotiated\n",
				    can_id);
			goto putback;
		}
		if (can_flags & VIRTIO_CAN_FLAGS_FD) {
			stats->rx_dropped++;
			netdev_warn(dev, "RX: CAN Id 0x%08x: RTR with FD not possible\n",
				    can_id);
			goto putback;
		}

		if (len > 0xF) {
			stats->rx_dropped++;
			netdev_warn(dev, "RX: CAN Id 0x%08x: RTR with DLC > 0xF\n",
				    can_id);
			goto putback;
		}

		if (len > 0x8)
			len = 0x8;

		can_id |= CAN_RTR_FLAG;
	}

	if (transport_len < header_size + len) {
		netdev_warn(dev, "RX: Message too small for payload\n");
		goto putback;
	}

	if (can_flags & VIRTIO_CAN_FLAGS_FD) {
		if (!virtio_has_feature(vq->vdev, VIRTIO_CAN_F_CAN_FD)) {
			stats->rx_dropped++;
			netdev_warn(dev, "RX: CAN Id 0x%08x: FD not negotiated\n",
				    can_id);
			goto putback;
		}

		if (len > CANFD_MAX_DLEN)
			len = CANFD_MAX_DLEN;

		skb = alloc_canfd_skb(priv->dev, &cf);
	} else {
		if (!virtio_has_feature(vq->vdev, VIRTIO_CAN_F_CAN_CLASSIC)) {
			stats->rx_dropped++;
			netdev_warn(dev, "RX: CAN Id 0x%08x: classic not negotiated\n",
				    can_id);
			goto putback;
		}

		if (len > CAN_MAX_DLEN)
			len = CAN_MAX_DLEN;

		skb = alloc_can_skb(priv->dev, (struct can_frame **)&cf);
	}
	if (!skb) {
		stats->rx_dropped++;
		netdev_warn(dev, "RX: No skb available\n");
		goto putback;
	}

	cf->can_id = can_id;
	cf->len = len;
	if (!(can_flags & VIRTIO_CAN_FLAGS_RTR)) {
		/* RTR frames have a DLC but no payload */
		memcpy(cf->data, can_rx->sdu, len);
	}

	if (netif_receive_skb(skb) == NET_RX_SUCCESS) {
		stats->rx_packets++;
		if (!(can_flags & VIRTIO_CAN_FLAGS_RTR))
			stats->rx_bytes += len;
	}

putback:
	/* Put processed RX buffer back into avail queue */
	ret = virtio_can_add_inbuf(vq, can_rx,
				   priv->rpkt_len);
	if (!ret)
		virtqueue_kick(vq);
	return 1; /* Queue was not empty so there may be more data */
}

static int virtio_can_handle_busoff(struct net_device *dev)
{
	struct virtio_can_priv *priv = netdev_priv(dev);
	struct can_frame *cf;
	struct sk_buff *skb;

	if (!priv->busoff_pending)
		return 0;

	if (priv->can.state < CAN_STATE_BUS_OFF) {
		netdev_dbg(dev, "entered error bus off state\n");

		/* bus-off state */
		priv->can.state = CAN_STATE_BUS_OFF;
		priv->can.can_stats.bus_off++;
		can_bus_off(dev);
	}

	/* propagate the error condition to the CAN stack */
	skb = alloc_can_err_skb(dev, &cf);
	if (unlikely(!skb))
		return 0;

	/* bus-off state */
	cf->can_id |= CAN_ERR_BUSOFF;

	/* Ensure that the BusOff indication does not get lost */
	if (netif_receive_skb(skb) == NET_RX_SUCCESS)
		priv->busoff_pending = false;

	return 1;
}

static int virtio_can_rx_poll(struct napi_struct *napi, int quota)
{
	struct net_device *dev = napi->dev;
	struct virtio_can_priv *priv = netdev_priv(dev);
	struct virtqueue *vq = priv->vqs[VIRTIO_CAN_QUEUE_RX];
	int work_done = 0;

	work_done += virtio_can_handle_busoff(dev);

	while (work_done < quota && virtio_can_read_rx_queue(vq) != 0)
		work_done++;

	if (work_done < quota)
		virtqueue_napi_complete(napi, vq, work_done);

	return work_done;
}

static void virtio_can_rx_intr(struct virtqueue *vq)
{
	struct virtio_can_priv *can_priv = vq->vdev->priv;

	virtqueue_disable_cb(vq);
	napi_schedule(&can_priv->napi);
}

static void virtio_can_control_intr(struct virtqueue *vq)
{
	struct virtio_can_priv *can_priv = vq->vdev->priv;

	complete(&can_priv->ctrl_done);
}

static void virtio_can_config_changed(struct virtio_device *vdev)
{
	struct virtio_can_priv *can_priv = vdev->priv;
	u16 status;

	status = virtio_cread16(vdev, offsetof(struct virtio_can_config,
					       status));

	if (!(status & VIRTIO_CAN_S_CTRL_BUSOFF))
		return;

	if (!can_priv->busoff_pending &&
	    can_priv->can.state < CAN_STATE_BUS_OFF) {
		can_priv->busoff_pending = true;
		napi_schedule(&can_priv->napi);
	}
}

static void virtio_can_populate_rx_vq(struct virtio_device *vdev)
{
	struct virtio_can_priv *priv = vdev->priv;
	struct virtqueue *vq = priv->vqs[VIRTIO_CAN_QUEUE_RX];
	unsigned int buf_size = priv->rpkt_len;
	int num_elements = vq->num_free;
	u8 *buf = (u8 *)priv->rpkt;
	unsigned int idx;
	int ret = 0;

	for (idx = 0; idx < num_elements; idx++) {
		ret = virtio_can_add_inbuf(vq, buf, buf_size);
		if (ret < 0) {
			dev_dbg(&vdev->dev, "rpkt fill: ret=%d, idx=%u, size=%u\n",
				ret, idx, buf_size);
			break;
		}
		buf += buf_size;
	}

	if (idx > 0)
		virtqueue_kick(vq);

	dev_dbg(&vdev->dev, "%u rpkt added\n", idx);
}

static int virtio_can_find_vqs(struct virtio_can_priv *priv)
{
	struct virtqueue_info vqs_info[] = {
		{ "can-tx", virtio_can_tx_intr },
		{ "can-rx", virtio_can_rx_intr },
		{ "can-state-ctrl", virtio_can_control_intr },
	};

	/* Find the queues. */
	return virtio_find_vqs(priv->vdev, VIRTIO_CAN_QUEUE_COUNT, priv->vqs,
			       vqs_info, NULL);
}

/* Function must not be called before virtio_can_find_vqs() has been run */
static void virtio_can_del_vq(struct virtio_device *vdev)
{
	struct virtio_can_priv *priv = vdev->priv;
	struct virtqueue *vq = priv->vqs[VIRTIO_CAN_QUEUE_TX];
	struct virtio_can_tx *can_tx_msg;
	int q;

	if (!vq)
		return;

	/* Reset the device */
	virtio_reset_device(vdev);

	/* From here we have dead silence from the device side so no locks
	 * are needed to protect against device side events.
	 */

	/* Free pending TX buffers which were allocated in virtio_can_start_xmit() */
	while ((can_tx_msg = virtqueue_detach_unused_buf(vq))) {
		can_free_echo_skb(priv->dev, can_tx_msg->putidx, NULL);
		virtio_can_free_tx_idx(priv, can_tx_msg->putidx);
		kfree(can_tx_msg);
	}

	/* RX and control queue buffers are managed elsewhere, just detach */
	for (q = VIRTIO_CAN_QUEUE_RX; q < VIRTIO_CAN_QUEUE_COUNT; q++)
		while (virtqueue_detach_unused_buf(priv->vqs[q]))
			;

	if (vdev->config->del_vqs)
		vdev->config->del_vqs(vdev);

	memset(priv->vqs, 0, sizeof(priv->vqs));
}

static void virtio_can_remove(struct virtio_device *vdev)
{
	struct virtio_can_priv *priv = vdev->priv;
	struct net_device *dev = priv->dev;

	unregister_candev(dev);

	virtio_can_del_vq(vdev);

	virtio_can_free_candev(dev);
}

static int virtio_can_validate(struct virtio_device *vdev)
{
	/* CAN needs always access to the config space.
	 * Check that the driver can access the config space
	 */
	if (!vdev->config->get) {
		dev_err(&vdev->dev, "%s failure: config access disabled\n",
			__func__);
		return -EINVAL;
	}

	if (!virtio_has_feature(vdev, VIRTIO_F_VERSION_1)) {
		dev_err(&vdev->dev,
			"device does not comply with spec version 1.x\n");
		return -EINVAL;
	}

	return 0;
}

static int virtio_can_probe(struct virtio_device *vdev)
{
	struct virtio_can_priv *priv;
	struct net_device *dev;
	size_t size;
	int err;

	dev = alloc_candev(sizeof(struct virtio_can_priv),
			   VIRTIO_CAN_ECHO_SKB_MAX);
	if (!dev)
		return -ENOMEM;

	priv = netdev_priv(dev);

	ida_init(&priv->tx_putidx_ida);

	netif_napi_add(dev, &priv->napi, virtio_can_rx_poll);
	netif_napi_add(dev, &priv->napi_tx, virtio_can_tx_poll);

	SET_NETDEV_DEV(dev, &vdev->dev);

	priv->dev = dev;
	priv->vdev = vdev;
	vdev->priv = priv;

	priv->can.do_set_mode = virtio_can_set_mode;
	priv->can.bittiming.bitrate = CAN_BITRATE_UNKNOWN;
	/* Set Virtio CAN supported operations */
	priv->can.ctrlmode_supported = CAN_CTRLMODE_BERR_REPORTING;
	if (virtio_has_feature(vdev, VIRTIO_CAN_F_CAN_FD)) {
		priv->can.fd.data_bittiming.bitrate = CAN_BITRATE_UNKNOWN;
		err = can_set_static_ctrlmode(dev, CAN_CTRLMODE_FD);
		if (err != 0)
			goto on_failure;
	}

	/* Initialize virtqueues */
	err = virtio_can_find_vqs(priv);
	if (err != 0)
		goto on_failure;

	spin_lock_init(&priv->tx_lock);
	mutex_init(&priv->ctrl_lock);

	init_completion(&priv->ctrl_done);

	priv->rpkt_len = sizeof(struct virtio_can_rx);

	if (virtio_has_feature(vdev, VIRTIO_CAN_F_CAN_FD))
		priv->rpkt_len += CANFD_MAX_DLEN;
	else
		priv->rpkt_len += CAN_MAX_DLEN;

	size = priv->rpkt_len * priv->vqs[VIRTIO_CAN_QUEUE_RX]->num_free;
	priv->rpkt = devm_kzalloc(&vdev->dev, size, GFP_KERNEL);
	if (!priv->rpkt) {
		virtio_can_del_vq(vdev);
		err = -ENOMEM;
		goto on_failure;
	}
	virtio_can_populate_rx_vq(vdev);

	err = register_virtio_can_dev(dev);
	if (err) {
		virtio_can_del_vq(vdev);
		goto on_failure;
	}

	return 0;

on_failure:
	virtio_can_free_candev(dev);
	return err;
}

static int __maybe_unused virtio_can_freeze(struct virtio_device *vdev)
{
	struct virtio_can_priv *priv = vdev->priv;
	struct net_device *ndev = priv->dev;

	if (netif_running(ndev)) {
		/* virtio_can_close() calls netif_stop_queue(), virtio_can_stop(),
		 * napi_disable() and close_candev(). Call it directly (not via
		 * dev_close()) to preserve IFF_UP so that netif_running() returns
		 * true in virtio_can_restore() and the device is brought back up.
		 */
		virtio_can_close(ndev);
		netif_device_detach(ndev);
	}

	priv->can.state = CAN_STATE_SLEEPING;

	virtio_can_del_vq(vdev);

	return 0;
}

static int __maybe_unused virtio_can_restore(struct virtio_device *vdev)
{
	struct virtio_can_priv *priv = vdev->priv;
	struct net_device *ndev = priv->dev;
	size_t size;
	int err;

	err = virtio_can_find_vqs(priv);
	if (err != 0)
		return err;

	size = priv->rpkt_len * priv->vqs[VIRTIO_CAN_QUEUE_RX]->num_free;
	priv->rpkt = devm_krealloc(&vdev->dev, priv->rpkt, size, GFP_KERNEL | __GFP_ZERO);
	if (!priv->rpkt) {
		virtio_can_del_vq(vdev);
		return -ENOMEM;
	}
	virtio_can_populate_rx_vq(vdev);

	if (netif_running(ndev)) {
		/* virtio_can_open() calls open_candev(), virtio_can_start(),
		 * napi_enable() and netif_start_queue(). Call it directly (not
		 * via dev_open()) since IFF_UP is still set from before freeze.
		 */
		err = virtio_can_open(ndev);
		if (err) {
			virtio_can_del_vq(vdev);
			return err;
		}
		netif_device_attach(ndev);
	} else {
		priv->can.state = CAN_STATE_STOPPED;
	}

	return 0;
}

static struct virtio_device_id virtio_can_id_table[] = {
	{ VIRTIO_ID_CAN, VIRTIO_DEV_ANY_ID },
	{ 0 },
};

static unsigned int features[] = {
	VIRTIO_CAN_F_CAN_CLASSIC,
	VIRTIO_CAN_F_CAN_FD,
	VIRTIO_CAN_F_LATE_TX_ACK,
	VIRTIO_CAN_F_RTR_FRAMES,
};

static struct virtio_driver virtio_can_driver = {
	.feature_table = features,
	.feature_table_size = ARRAY_SIZE(features),
	.driver.name = KBUILD_MODNAME,
	.driver.owner = THIS_MODULE,
	.id_table = virtio_can_id_table,
	.validate = virtio_can_validate,
	.probe = virtio_can_probe,
	.remove = virtio_can_remove,
	.config_changed = virtio_can_config_changed,
#ifdef CONFIG_PM_SLEEP
	.freeze = virtio_can_freeze,
	.restore = virtio_can_restore,
#endif
};

module_virtio_driver(virtio_can_driver);
MODULE_DEVICE_TABLE(virtio, virtio_can_id_table);

MODULE_AUTHOR("OpenSynergy GmbH");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("CAN bus driver for Virtio CAN controller");
