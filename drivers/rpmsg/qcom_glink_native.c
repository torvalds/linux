/*
 * Copyright (c) 2016-2017, Linaro Ltd
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/idr.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/list.h>
#include <linux/mfd/syscon.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/rpmsg.h>
#include <linux/slab.h>
#include <linux/workqueue.h>
#include <linux/mailbox_client.h>

#include "rpmsg_internal.h"
#include "qcom_glink_native.h"

#define GLINK_NAME_SIZE		32
#define GLINK_VERSION_1		1

#define RPM_GLINK_CID_MIN	1
#define RPM_GLINK_CID_MAX	65536

struct glink_msg {
	__le16 cmd;
	__le16 param1;
	__le32 param2;
	u8 data[];
} __packed;

/**
 * struct glink_defer_cmd - deferred incoming control message
 * @node:	list node
 * @msg:	message header
 * data:	payload of the message
 *
 * Copy of a received control message, to be added to @rx_queue and processed
 * by @rx_work of @qcom_glink.
 */
struct glink_defer_cmd {
	struct list_head node;

	struct glink_msg msg;
	u8 data[];
};

/**
 * struct qcom_glink - driver context, relates to one remote subsystem
 * @dev:	reference to the associated struct device
 * @mbox_client: mailbox client
 * @mbox_chan:  mailbox channel
 * @rx_pipe:	pipe object for receive FIFO
 * @tx_pipe:	pipe object for transmit FIFO
 * @irq:	IRQ for signaling incoming events
 * @rx_work:	worker for handling received control messages
 * @rx_lock:	protects the @rx_queue
 * @rx_queue:	queue of received control messages to be processed in @rx_work
 * @tx_lock:	synchronizes operations on the tx fifo
 * @idr_lock:	synchronizes @lcids and @rcids modifications
 * @lcids:	idr of all channels with a known local channel id
 * @rcids:	idr of all channels with a known remote channel id
 */
struct qcom_glink {
	struct device *dev;

	struct mbox_client mbox_client;
	struct mbox_chan *mbox_chan;

	struct qcom_glink_pipe *rx_pipe;
	struct qcom_glink_pipe *tx_pipe;

	int irq;

	struct work_struct rx_work;
	spinlock_t rx_lock;
	struct list_head rx_queue;

	struct mutex tx_lock;

	spinlock_t idr_lock;
	struct idr lcids;
	struct idr rcids;
	unsigned long features;

	bool intentless;
};

enum {
	GLINK_STATE_CLOSED,
	GLINK_STATE_OPENING,
	GLINK_STATE_OPEN,
	GLINK_STATE_CLOSING,
};

/**
 * struct glink_channel - internal representation of a channel
 * @rpdev:	rpdev reference, only used for primary endpoints
 * @ept:	rpmsg endpoint this channel is associated with
 * @glink:	qcom_glink context handle
 * @refcount:	refcount for the channel object
 * @recv_lock:	guard for @ept.cb
 * @name:	unique channel name/identifier
 * @lcid:	channel id, in local space
 * @rcid:	channel id, in remote space
 * @buf:	receive buffer, for gathering fragments
 * @buf_offset:	write offset in @buf
 * @buf_size:	size of current @buf
 * @open_ack:	completed once remote has acked the open-request
 * @open_req:	completed once open-request has been received
 */
struct glink_channel {
	struct rpmsg_endpoint ept;

	struct rpmsg_device *rpdev;
	struct qcom_glink *glink;

	struct kref refcount;

	spinlock_t recv_lock;

	char *name;
	unsigned int lcid;
	unsigned int rcid;

	void *buf;
	int buf_offset;
	int buf_size;

	struct completion open_ack;
	struct completion open_req;
};

#define to_glink_channel(_ept) container_of(_ept, struct glink_channel, ept)

static const struct rpmsg_endpoint_ops glink_endpoint_ops;

#define RPM_CMD_VERSION			0
#define RPM_CMD_VERSION_ACK		1
#define RPM_CMD_OPEN			2
#define RPM_CMD_CLOSE			3
#define RPM_CMD_OPEN_ACK		4
#define RPM_CMD_TX_DATA			9
#define RPM_CMD_CLOSE_ACK		11
#define RPM_CMD_TX_DATA_CONT		12
#define RPM_CMD_READ_NOTIF		13

#define GLINK_FEATURE_INTENTLESS	BIT(1)

static struct glink_channel *qcom_glink_alloc_channel(struct qcom_glink *glink,
						      const char *name)
{
	struct glink_channel *channel;

	channel = kzalloc(sizeof(*channel), GFP_KERNEL);
	if (!channel)
		return ERR_PTR(-ENOMEM);

	/* Setup glink internal glink_channel data */
	spin_lock_init(&channel->recv_lock);
	channel->glink = glink;
	channel->name = kstrdup(name, GFP_KERNEL);

	init_completion(&channel->open_req);
	init_completion(&channel->open_ack);

	kref_init(&channel->refcount);

	return channel;
}

static void qcom_glink_channel_release(struct kref *ref)
{
	struct glink_channel *channel = container_of(ref, struct glink_channel,
						     refcount);

	kfree(channel->name);
	kfree(channel);
}

static size_t qcom_glink_rx_avail(struct qcom_glink *glink)
{
	return glink->rx_pipe->avail(glink->rx_pipe);
}

static void qcom_glink_rx_peak(struct qcom_glink *glink,
			       void *data, size_t count)
{
	glink->rx_pipe->peak(glink->rx_pipe, data, count);
}

static void qcom_glink_rx_advance(struct qcom_glink *glink, size_t count)
{
	glink->rx_pipe->advance(glink->rx_pipe, count);
}

static size_t qcom_glink_tx_avail(struct qcom_glink *glink)
{
	return glink->tx_pipe->avail(glink->tx_pipe);
}

static void qcom_glink_tx_write(struct qcom_glink *glink,
				const void *hdr, size_t hlen,
				const void *data, size_t dlen)
{
	glink->tx_pipe->write(glink->tx_pipe, hdr, hlen, data, dlen);
}

static int qcom_glink_tx(struct qcom_glink *glink,
			 const void *hdr, size_t hlen,
			 const void *data, size_t dlen, bool wait)
{
	unsigned int tlen = hlen + dlen;
	int ret;

	/* Reject packets that are too big */
	if (tlen >= glink->tx_pipe->length)
		return -EINVAL;

	ret = mutex_lock_interruptible(&glink->tx_lock);
	if (ret)
		return ret;

	while (qcom_glink_tx_avail(glink) < tlen) {
		if (!wait) {
			ret = -EAGAIN;
			goto out;
		}

		usleep_range(10000, 15000);
	}

	qcom_glink_tx_write(glink, hdr, hlen, data, dlen);

	mbox_send_message(glink->mbox_chan, NULL);
	mbox_client_txdone(glink->mbox_chan, 0);

out:
	mutex_unlock(&glink->tx_lock);

	return ret;
}

static int qcom_glink_send_version(struct qcom_glink *glink)
{
	struct glink_msg msg;

	msg.cmd = cpu_to_le16(RPM_CMD_VERSION);
	msg.param1 = cpu_to_le16(GLINK_VERSION_1);
	msg.param2 = cpu_to_le32(glink->features);

	return qcom_glink_tx(glink, &msg, sizeof(msg), NULL, 0, true);
}

static void qcom_glink_send_version_ack(struct qcom_glink *glink)
{
	struct glink_msg msg;

	msg.cmd = cpu_to_le16(RPM_CMD_VERSION_ACK);
	msg.param1 = cpu_to_le16(GLINK_VERSION_1);
	msg.param2 = cpu_to_le32(glink->features);

	qcom_glink_tx(glink, &msg, sizeof(msg), NULL, 0, true);
}

static void qcom_glink_send_open_ack(struct qcom_glink *glink,
				     struct glink_channel *channel)
{
	struct glink_msg msg;

	msg.cmd = cpu_to_le16(RPM_CMD_OPEN_ACK);
	msg.param1 = cpu_to_le16(channel->rcid);
	msg.param2 = cpu_to_le32(0);

	qcom_glink_tx(glink, &msg, sizeof(msg), NULL, 0, true);
}

/**
 * qcom_glink_send_open_req() - send a RPM_CMD_OPEN request to the remote
 * @glink: Ptr to the glink edge
 * @channel: Ptr to the channel that the open req is sent
 *
 * Allocates a local channel id and sends a RPM_CMD_OPEN message to the remote.
 * Will return with refcount held, regardless of outcome.
 *
 * Returns 0 on success, negative errno otherwise.
 */
static int qcom_glink_send_open_req(struct qcom_glink *glink,
				    struct glink_channel *channel)
{
	struct {
		struct glink_msg msg;
		u8 name[GLINK_NAME_SIZE];
	} __packed req;
	int name_len = strlen(channel->name) + 1;
	int req_len = ALIGN(sizeof(req.msg) + name_len, 8);
	int ret;
	unsigned long flags;

	kref_get(&channel->refcount);

	spin_lock_irqsave(&glink->idr_lock, flags);
	ret = idr_alloc_cyclic(&glink->lcids, channel,
			       RPM_GLINK_CID_MIN, RPM_GLINK_CID_MAX,
			       GFP_ATOMIC);
	spin_unlock_irqrestore(&glink->idr_lock, flags);
	if (ret < 0)
		return ret;

	channel->lcid = ret;

	req.msg.cmd = cpu_to_le16(RPM_CMD_OPEN);
	req.msg.param1 = cpu_to_le16(channel->lcid);
	req.msg.param2 = cpu_to_le32(name_len);
	strcpy(req.name, channel->name);

	ret = qcom_glink_tx(glink, &req, req_len, NULL, 0, true);
	if (ret)
		goto remove_idr;

	return 0;

remove_idr:
	spin_lock_irqsave(&glink->idr_lock, flags);
	idr_remove(&glink->lcids, channel->lcid);
	channel->lcid = 0;
	spin_unlock_irqrestore(&glink->idr_lock, flags);

	return ret;
}

static void qcom_glink_send_close_req(struct qcom_glink *glink,
				      struct glink_channel *channel)
{
	struct glink_msg req;

	req.cmd = cpu_to_le16(RPM_CMD_CLOSE);
	req.param1 = cpu_to_le16(channel->lcid);
	req.param2 = 0;

	qcom_glink_tx(glink, &req, sizeof(req), NULL, 0, true);
}

static void qcom_glink_send_close_ack(struct qcom_glink *glink,
				      unsigned int rcid)
{
	struct glink_msg req;

	req.cmd = cpu_to_le16(RPM_CMD_CLOSE_ACK);
	req.param1 = cpu_to_le16(rcid);
	req.param2 = 0;

	qcom_glink_tx(glink, &req, sizeof(req), NULL, 0, true);
}

/**
 * qcom_glink_receive_version() - receive version/features from remote system
 *
 * @glink:	pointer to transport interface
 * @r_version:	remote version
 * @r_features:	remote features
 *
 * This function is called in response to a remote-initiated version/feature
 * negotiation sequence.
 */
static void qcom_glink_receive_version(struct qcom_glink *glink,
				       u32 version,
				       u32 features)
{
	switch (version) {
	case 0:
		break;
	case GLINK_VERSION_1:
		glink->features &= features;
		/* FALLTHROUGH */
	default:
		qcom_glink_send_version_ack(glink);
		break;
	}
}

/**
 * qcom_glink_receive_version_ack() - receive negotiation ack from remote system
 *
 * @glink:	pointer to transport interface
 * @r_version:	remote version response
 * @r_features:	remote features response
 *
 * This function is called in response to a local-initiated version/feature
 * negotiation sequence and is the counter-offer from the remote side based
 * upon the initial version and feature set requested.
 */
static void qcom_glink_receive_version_ack(struct qcom_glink *glink,
					   u32 version,
					   u32 features)
{
	switch (version) {
	case 0:
		/* Version negotiation failed */
		break;
	case GLINK_VERSION_1:
		if (features == glink->features)
			break;

		glink->features &= features;
		/* FALLTHROUGH */
	default:
		qcom_glink_send_version(glink);
		break;
	}
}

static int qcom_glink_rx_defer(struct qcom_glink *glink, size_t extra)
{
	struct glink_defer_cmd *dcmd;

	extra = ALIGN(extra, 8);

	if (qcom_glink_rx_avail(glink) < sizeof(struct glink_msg) + extra) {
		dev_dbg(glink->dev, "Insufficient data in rx fifo");
		return -ENXIO;
	}

	dcmd = kzalloc(sizeof(*dcmd) + extra, GFP_ATOMIC);
	if (!dcmd)
		return -ENOMEM;

	INIT_LIST_HEAD(&dcmd->node);

	qcom_glink_rx_peak(glink, &dcmd->msg, sizeof(dcmd->msg) + extra);

	spin_lock(&glink->rx_lock);
	list_add_tail(&dcmd->node, &glink->rx_queue);
	spin_unlock(&glink->rx_lock);

	schedule_work(&glink->rx_work);
	qcom_glink_rx_advance(glink, sizeof(dcmd->msg) + extra);

	return 0;
}

static int qcom_glink_rx_data(struct qcom_glink *glink, size_t avail)
{
	struct glink_channel *channel;
	struct {
		struct glink_msg msg;
		__le32 chunk_size;
		__le32 left_size;
	} __packed hdr;
	unsigned int chunk_size;
	unsigned int left_size;
	unsigned int rcid;
	unsigned long flags;

	if (avail < sizeof(hdr)) {
		dev_dbg(glink->dev, "Not enough data in fifo\n");
		return -EAGAIN;
	}

	qcom_glink_rx_peak(glink, &hdr, sizeof(hdr));
	chunk_size = le32_to_cpu(hdr.chunk_size);
	left_size = le32_to_cpu(hdr.left_size);

	if (avail < sizeof(hdr) + chunk_size) {
		dev_dbg(glink->dev, "Payload not yet in fifo\n");
		return -EAGAIN;
	}

	if (WARN(chunk_size % 4, "Incoming data must be word aligned\n"))
		return -EINVAL;

	rcid = le16_to_cpu(hdr.msg.param1);
	spin_lock_irqsave(&glink->idr_lock, flags);
	channel = idr_find(&glink->rcids, rcid);
	spin_unlock_irqrestore(&glink->idr_lock, flags);
	if (!channel) {
		dev_dbg(glink->dev, "Data on non-existing channel\n");

		/* Drop the message */
		qcom_glink_rx_advance(glink,
				      ALIGN(sizeof(hdr) + chunk_size, 8));
		return 0;
	}

	/* Might have an ongoing, fragmented, message to append */
	if (!channel->buf) {
		channel->buf = kmalloc(chunk_size + left_size, GFP_ATOMIC);
		if (!channel->buf)
			return -ENOMEM;

		channel->buf_size = chunk_size + left_size;
		channel->buf_offset = 0;
	}

	qcom_glink_rx_advance(glink, sizeof(hdr));

	if (channel->buf_size - channel->buf_offset < chunk_size) {
		dev_err(glink->dev, "Insufficient space in input buffer\n");

		/* The packet header lied, drop payload */
		qcom_glink_rx_advance(glink, chunk_size);
		return -ENOMEM;
	}

	qcom_glink_rx_peak(glink, channel->buf + channel->buf_offset,
			   chunk_size);
	channel->buf_offset += chunk_size;

	/* Handle message when no fragments remain to be received */
	if (!left_size) {
		spin_lock(&channel->recv_lock);
		if (channel->ept.cb) {
			channel->ept.cb(channel->ept.rpdev,
					channel->buf,
					channel->buf_offset,
					channel->ept.priv,
					RPMSG_ADDR_ANY);
		}
		spin_unlock(&channel->recv_lock);

		kfree(channel->buf);
		channel->buf = NULL;
		channel->buf_size = 0;
	}

	/* Each message starts at 8 byte aligned address */
	qcom_glink_rx_advance(glink, ALIGN(chunk_size, 8));

	return 0;
}

static int qcom_glink_rx_open_ack(struct qcom_glink *glink, unsigned int lcid)
{
	struct glink_channel *channel;

	spin_lock(&glink->idr_lock);
	channel = idr_find(&glink->lcids, lcid);
	if (!channel) {
		dev_err(glink->dev, "Invalid open ack packet\n");
		return -EINVAL;
	}
	spin_unlock(&glink->idr_lock);

	complete(&channel->open_ack);

	return 0;
}

static irqreturn_t qcom_glink_native_intr(int irq, void *data)
{
	struct qcom_glink *glink = data;
	struct glink_msg msg;
	unsigned int param1;
	unsigned int param2;
	unsigned int avail;
	unsigned int cmd;
	int ret;

	for (;;) {
		avail = qcom_glink_rx_avail(glink);
		if (avail < sizeof(msg))
			break;

		qcom_glink_rx_peak(glink, &msg, sizeof(msg));

		cmd = le16_to_cpu(msg.cmd);
		param1 = le16_to_cpu(msg.param1);
		param2 = le32_to_cpu(msg.param2);

		switch (cmd) {
		case RPM_CMD_VERSION:
		case RPM_CMD_VERSION_ACK:
		case RPM_CMD_CLOSE:
		case RPM_CMD_CLOSE_ACK:
			ret = qcom_glink_rx_defer(glink, 0);
			break;
		case RPM_CMD_OPEN_ACK:
			ret = qcom_glink_rx_open_ack(glink, param1);
			qcom_glink_rx_advance(glink, ALIGN(sizeof(msg), 8));
			break;
		case RPM_CMD_OPEN:
			ret = qcom_glink_rx_defer(glink, param2);
			break;
		case RPM_CMD_TX_DATA:
		case RPM_CMD_TX_DATA_CONT:
			ret = qcom_glink_rx_data(glink, avail);
			break;
		case RPM_CMD_READ_NOTIF:
			qcom_glink_rx_advance(glink, ALIGN(sizeof(msg), 8));

			mbox_send_message(glink->mbox_chan, NULL);
			mbox_client_txdone(glink->mbox_chan, 0);

			ret = 0;
			break;
		default:
			dev_err(glink->dev, "unhandled rx cmd: %d\n", cmd);
			ret = -EINVAL;
			break;
		}

		if (ret)
			break;
	}

	return IRQ_HANDLED;
}

/* Locally initiated rpmsg_create_ept */
static struct glink_channel *qcom_glink_create_local(struct qcom_glink *glink,
						     const char *name)
{
	struct glink_channel *channel;
	int ret;
	unsigned long flags;

	channel = qcom_glink_alloc_channel(glink, name);
	if (IS_ERR(channel))
		return ERR_CAST(channel);

	ret = qcom_glink_send_open_req(glink, channel);
	if (ret)
		goto release_channel;

	ret = wait_for_completion_timeout(&channel->open_ack, 5 * HZ);
	if (!ret)
		goto err_timeout;

	ret = wait_for_completion_timeout(&channel->open_req, 5 * HZ);
	if (!ret)
		goto err_timeout;

	qcom_glink_send_open_ack(glink, channel);

	return channel;

err_timeout:
	/* qcom_glink_send_open_req() did register the channel in lcids*/
	spin_lock_irqsave(&glink->idr_lock, flags);
	idr_remove(&glink->lcids, channel->lcid);
	spin_unlock_irqrestore(&glink->idr_lock, flags);

release_channel:
	/* Release qcom_glink_send_open_req() reference */
	kref_put(&channel->refcount, qcom_glink_channel_release);
	/* Release qcom_glink_alloc_channel() reference */
	kref_put(&channel->refcount, qcom_glink_channel_release);

	return ERR_PTR(-ETIMEDOUT);
}

/* Remote initiated rpmsg_create_ept */
static int qcom_glink_create_remote(struct qcom_glink *glink,
				    struct glink_channel *channel)
{
	int ret;

	qcom_glink_send_open_ack(glink, channel);

	ret = qcom_glink_send_open_req(glink, channel);
	if (ret)
		goto close_link;

	ret = wait_for_completion_timeout(&channel->open_ack, 5 * HZ);
	if (!ret) {
		ret = -ETIMEDOUT;
		goto close_link;
	}

	return 0;

close_link:
	/*
	 * Send a close request to "undo" our open-ack. The close-ack will
	 * release the last reference.
	 */
	qcom_glink_send_close_req(glink, channel);

	/* Release qcom_glink_send_open_req() reference */
	kref_put(&channel->refcount, qcom_glink_channel_release);

	return ret;
}

static struct rpmsg_endpoint *qcom_glink_create_ept(struct rpmsg_device *rpdev,
						    rpmsg_rx_cb_t cb,
						    void *priv,
						    struct rpmsg_channel_info
									chinfo)
{
	struct glink_channel *parent = to_glink_channel(rpdev->ept);
	struct glink_channel *channel;
	struct qcom_glink *glink = parent->glink;
	struct rpmsg_endpoint *ept;
	const char *name = chinfo.name;
	int cid;
	int ret;
	unsigned long flags;

	spin_lock_irqsave(&glink->idr_lock, flags);
	idr_for_each_entry(&glink->rcids, channel, cid) {
		if (!strcmp(channel->name, name))
			break;
	}
	spin_unlock_irqrestore(&glink->idr_lock, flags);

	if (!channel) {
		channel = qcom_glink_create_local(glink, name);
		if (IS_ERR(channel))
			return NULL;
	} else {
		ret = qcom_glink_create_remote(glink, channel);
		if (ret)
			return NULL;
	}

	ept = &channel->ept;
	ept->rpdev = rpdev;
	ept->cb = cb;
	ept->priv = priv;
	ept->ops = &glink_endpoint_ops;

	return ept;
}

static void qcom_glink_destroy_ept(struct rpmsg_endpoint *ept)
{
	struct glink_channel *channel = to_glink_channel(ept);
	struct qcom_glink *glink = channel->glink;
	unsigned long flags;

	spin_lock_irqsave(&channel->recv_lock, flags);
	channel->ept.cb = NULL;
	spin_unlock_irqrestore(&channel->recv_lock, flags);

	/* Decouple the potential rpdev from the channel */
	channel->rpdev = NULL;

	qcom_glink_send_close_req(glink, channel);
}

static int __qcom_glink_send(struct glink_channel *channel,
			     void *data, int len, bool wait)
{
	struct qcom_glink *glink = channel->glink;
	struct {
		struct glink_msg msg;
		__le32 chunk_size;
		__le32 left_size;
	} __packed req;

	req.msg.cmd = cpu_to_le16(RPM_CMD_TX_DATA);
	req.msg.param1 = cpu_to_le16(channel->lcid);
	req.msg.param2 = cpu_to_le32(channel->rcid);
	req.chunk_size = cpu_to_le32(len);
	req.left_size = cpu_to_le32(0);

	return qcom_glink_tx(glink, &req, sizeof(req), data, len, wait);
}

static int qcom_glink_send(struct rpmsg_endpoint *ept, void *data, int len)
{
	struct glink_channel *channel = to_glink_channel(ept);

	return __qcom_glink_send(channel, data, len, true);
}

static int qcom_glink_trysend(struct rpmsg_endpoint *ept, void *data, int len)
{
	struct glink_channel *channel = to_glink_channel(ept);

	return __qcom_glink_send(channel, data, len, false);
}

/*
 * Finds the device_node for the glink child interested in this channel.
 */
static struct device_node *qcom_glink_match_channel(struct device_node *node,
						    const char *channel)
{
	struct device_node *child;
	const char *name;
	const char *key;
	int ret;

	for_each_available_child_of_node(node, child) {
		key = "qcom,glink-channels";
		ret = of_property_read_string(child, key, &name);
		if (ret)
			continue;

		if (strcmp(name, channel) == 0)
			return child;
	}

	return NULL;
}

static const struct rpmsg_device_ops glink_device_ops = {
	.create_ept = qcom_glink_create_ept,
};

static const struct rpmsg_endpoint_ops glink_endpoint_ops = {
	.destroy_ept = qcom_glink_destroy_ept,
	.send = qcom_glink_send,
	.trysend = qcom_glink_trysend,
};

static void qcom_glink_rpdev_release(struct device *dev)
{
	struct rpmsg_device *rpdev = to_rpmsg_device(dev);
	struct glink_channel *channel = to_glink_channel(rpdev->ept);

	channel->rpdev = NULL;
	kfree(rpdev);
}

static int qcom_glink_rx_open(struct qcom_glink *glink, unsigned int rcid,
			      char *name)
{
	struct glink_channel *channel;
	struct rpmsg_device *rpdev;
	bool create_device = false;
	struct device_node *node;
	int lcid;
	int ret;
	unsigned long flags;

	spin_lock_irqsave(&glink->idr_lock, flags);
	idr_for_each_entry(&glink->lcids, channel, lcid) {
		if (!strcmp(channel->name, name))
			break;
	}
	spin_unlock_irqrestore(&glink->idr_lock, flags);

	if (!channel) {
		channel = qcom_glink_alloc_channel(glink, name);
		if (IS_ERR(channel))
			return PTR_ERR(channel);

		/* The opening dance was initiated by the remote */
		create_device = true;
	}

	spin_lock_irqsave(&glink->idr_lock, flags);
	ret = idr_alloc(&glink->rcids, channel, rcid, rcid + 1, GFP_ATOMIC);
	if (ret < 0) {
		dev_err(glink->dev, "Unable to insert channel into rcid list\n");
		spin_unlock_irqrestore(&glink->idr_lock, flags);
		goto free_channel;
	}
	channel->rcid = ret;
	spin_unlock_irqrestore(&glink->idr_lock, flags);

	complete(&channel->open_req);

	if (create_device) {
		rpdev = kzalloc(sizeof(*rpdev), GFP_KERNEL);
		if (!rpdev) {
			ret = -ENOMEM;
			goto rcid_remove;
		}

		rpdev->ept = &channel->ept;
		strncpy(rpdev->id.name, name, RPMSG_NAME_SIZE);
		rpdev->src = RPMSG_ADDR_ANY;
		rpdev->dst = RPMSG_ADDR_ANY;
		rpdev->ops = &glink_device_ops;

		node = qcom_glink_match_channel(glink->dev->of_node, name);
		rpdev->dev.of_node = node;
		rpdev->dev.parent = glink->dev;
		rpdev->dev.release = qcom_glink_rpdev_release;

		ret = rpmsg_register_device(rpdev);
		if (ret)
			goto free_rpdev;

		channel->rpdev = rpdev;
	}

	return 0;

free_rpdev:
	kfree(rpdev);
rcid_remove:
	spin_lock_irqsave(&glink->idr_lock, flags);
	idr_remove(&glink->rcids, channel->rcid);
	channel->rcid = 0;
	spin_unlock_irqrestore(&glink->idr_lock, flags);
free_channel:
	/* Release the reference, iff we took it */
	if (create_device)
		kref_put(&channel->refcount, qcom_glink_channel_release);

	return ret;
}

static void qcom_glink_rx_close(struct qcom_glink *glink, unsigned int rcid)
{
	struct rpmsg_channel_info chinfo;
	struct glink_channel *channel;
	unsigned long flags;

	spin_lock_irqsave(&glink->idr_lock, flags);
	channel = idr_find(&glink->rcids, rcid);
	spin_unlock_irqrestore(&glink->idr_lock, flags);
	if (WARN(!channel, "close request on unknown channel\n"))
		return;

	if (channel->rpdev) {
		strncpy(chinfo.name, channel->name, sizeof(chinfo.name));
		chinfo.src = RPMSG_ADDR_ANY;
		chinfo.dst = RPMSG_ADDR_ANY;

		rpmsg_unregister_device(glink->dev, &chinfo);
	}

	qcom_glink_send_close_ack(glink, channel->rcid);

	spin_lock_irqsave(&glink->idr_lock, flags);
	idr_remove(&glink->rcids, channel->rcid);
	channel->rcid = 0;
	spin_unlock_irqrestore(&glink->idr_lock, flags);

	kref_put(&channel->refcount, qcom_glink_channel_release);
}

static void qcom_glink_rx_close_ack(struct qcom_glink *glink, unsigned int lcid)
{
	struct glink_channel *channel;
	unsigned long flags;

	spin_lock_irqsave(&glink->idr_lock, flags);
	channel = idr_find(&glink->lcids, lcid);
	if (WARN(!channel, "close ack on unknown channel\n")) {
		spin_unlock_irqrestore(&glink->idr_lock, flags);
		return;
	}

	idr_remove(&glink->lcids, channel->lcid);
	channel->lcid = 0;
	spin_unlock_irqrestore(&glink->idr_lock, flags);

	kref_put(&channel->refcount, qcom_glink_channel_release);
}

static void qcom_glink_work(struct work_struct *work)
{
	struct qcom_glink *glink = container_of(work, struct qcom_glink,
						rx_work);
	struct glink_defer_cmd *dcmd;
	struct glink_msg *msg;
	unsigned long flags;
	unsigned int param1;
	unsigned int param2;
	unsigned int cmd;

	for (;;) {
		spin_lock_irqsave(&glink->rx_lock, flags);
		if (list_empty(&glink->rx_queue)) {
			spin_unlock_irqrestore(&glink->rx_lock, flags);
			break;
		}
		dcmd = list_first_entry(&glink->rx_queue,
					struct glink_defer_cmd, node);
		list_del(&dcmd->node);
		spin_unlock_irqrestore(&glink->rx_lock, flags);

		msg = &dcmd->msg;
		cmd = le16_to_cpu(msg->cmd);
		param1 = le16_to_cpu(msg->param1);
		param2 = le32_to_cpu(msg->param2);

		switch (cmd) {
		case RPM_CMD_VERSION:
			qcom_glink_receive_version(glink, param1, param2);
			break;
		case RPM_CMD_VERSION_ACK:
			qcom_glink_receive_version_ack(glink, param1, param2);
			break;
		case RPM_CMD_OPEN:
			qcom_glink_rx_open(glink, param1, msg->data);
			break;
		case RPM_CMD_CLOSE:
			qcom_glink_rx_close(glink, param1);
			break;
		case RPM_CMD_CLOSE_ACK:
			qcom_glink_rx_close_ack(glink, param1);
			break;
		default:
			WARN(1, "Unknown defer object %d\n", cmd);
			break;
		}

		kfree(dcmd);
	}
}

struct qcom_glink *qcom_glink_native_probe(struct device *dev,
					   unsigned long features,
					   struct qcom_glink_pipe *rx,
					   struct qcom_glink_pipe *tx)
{
	int irq;
	int ret;
	struct qcom_glink *glink;

	glink = devm_kzalloc(dev, sizeof(*glink), GFP_KERNEL);
	if (!glink)
		return ERR_PTR(-ENOMEM);

	glink->dev = dev;
	glink->tx_pipe = tx;
	glink->rx_pipe = rx;

	glink->features = features;

	mutex_init(&glink->tx_lock);
	spin_lock_init(&glink->rx_lock);
	INIT_LIST_HEAD(&glink->rx_queue);
	INIT_WORK(&glink->rx_work, qcom_glink_work);

	spin_lock_init(&glink->idr_lock);
	idr_init(&glink->lcids);
	idr_init(&glink->rcids);

	glink->mbox_client.dev = dev;
	glink->mbox_chan = mbox_request_channel(&glink->mbox_client, 0);
	if (IS_ERR(glink->mbox_chan)) {
		if (PTR_ERR(glink->mbox_chan) != -EPROBE_DEFER)
			dev_err(dev, "failed to acquire IPC channel\n");
		return ERR_CAST(glink->mbox_chan);
	}

	irq = of_irq_get(dev->of_node, 0);
	ret = devm_request_irq(dev, irq,
			       qcom_glink_native_intr,
			       IRQF_NO_SUSPEND | IRQF_SHARED,
			       "glink-native", glink);
	if (ret) {
		dev_err(dev, "failed to request IRQ\n");
		return ERR_PTR(ret);
	}

	glink->irq = irq;

	ret = qcom_glink_send_version(glink);
	if (ret)
		return ERR_PTR(ret);

	return glink;
}

static int qcom_glink_remove_device(struct device *dev, void *data)
{
	device_unregister(dev);

	return 0;
}

void qcom_glink_native_remove(struct qcom_glink *glink)
{
	struct glink_channel *channel;
	int cid;
	int ret;
	unsigned long flags;

	disable_irq(glink->irq);
	cancel_work_sync(&glink->rx_work);

	ret = device_for_each_child(glink->dev, NULL, qcom_glink_remove_device);
	if (ret)
		dev_warn(glink->dev, "Can't remove GLINK devices: %d\n", ret);

	spin_lock_irqsave(&glink->idr_lock, flags);
	/* Release any defunct local channels, waiting for close-ack */
	idr_for_each_entry(&glink->lcids, channel, cid)
		kref_put(&channel->refcount, qcom_glink_channel_release);

	idr_destroy(&glink->lcids);
	idr_destroy(&glink->rcids);
	spin_unlock_irqrestore(&glink->idr_lock, flags);
	mbox_free_channel(glink->mbox_chan);
}

void qcom_glink_native_unregister(struct qcom_glink *glink)
{
	device_unregister(glink->dev);
}
