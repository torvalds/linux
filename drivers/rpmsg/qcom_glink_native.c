// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2016-2017, Linaro Ltd
 * Copyright (c) 2022-2023 Qualcomm Innovation Center, Inc. All rights reserved.
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
#include <linux/sizes.h>
#include <linux/slab.h>
#include <linux/workqueue.h>
#include <linux/kthread.h>
#include <linux/mailbox_client.h>
#include <linux/suspend.h>
#include <linux/termios.h>
#include <linux/ipc_logging.h>

#include "rpmsg_internal.h"
#include "qcom_glink_native.h"

#define GLINK_LOG_PAGE_CNT 2
#define GLINK_INFO(ctxt, x, ...)					  \
	ipc_log_string(ctxt, "[%s]: "x, __func__, ##__VA_ARGS__)

#define CH_INFO(ch, x, ...)						     \
do {									     \
	if (ch->glink)							     \
		ipc_log_string(ch->glink->ilc, "%s[%d:%d] %s: "x, ch->name,  \
			       ch->lcid, ch->rcid, __func__, ##__VA_ARGS__); \
} while (0)

#define CH_ERR(ch, x, ...)						     \
do {									     \
	if (ch->glink) {						     \
		ipc_log_string(ch->glink->ilc, "%s[%d:%d] %s: "x, ch->name,  \
			       ch->lcid, ch->rcid, __func__, ##__VA_ARGS__); \
		dev_err_ratelimited(ch->glink->dev, "[%s]: "x, __func__, ##__VA_ARGS__); \
	}								     \
} while (0)

#if IS_ENABLED(CONFIG_RPMSG_QCOM_GLINK_DEBUG)
#define GLINK_BUG(ctxt, x, ...)						\
do {									\
	ipc_log_string(ctxt, "[%s]: ASSERT at line %d: "x,		\
		       __func__, __LINE__, ##__VA_ARGS__);		\
	pr_err("[%s]: ASSERT at line %d: "x,				\
		       __func__, __LINE__, ##__VA_ARGS__);		\
	BUG();								\
} while (0)
#else
#define GLINK_BUG(ctxt, x, ...)						\
do {									\
	ipc_log_string(ctxt, "[%s]: WARN at line %d: "x,		\
		       __func__, __LINE__, ##__VA_ARGS__);		\
	pr_err("[%s]: WARN at line %d: "x,				\
		       __func__, __LINE__, ##__VA_ARGS__);		\
} while (0)
#endif

#define GLINK_NAME_SIZE		32
#define GLINK_VERSION_1		1

#define RPM_GLINK_CID_MIN	1
#define RPM_GLINK_CID_MAX	65536

static int should_wake;
static int glink_resume_pkt;

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
 * @data:	payload of the message
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
 * struct glink_core_rx_intent - RX intent
 * RX intent
 *
 * @data: pointer to the data (may be NULL for zero-copy)
 * @id: remote or local intent ID
 * @size: size of the original intent (do not modify)
 * @reuse: To mark if the intent can be reused after first use
 * @in_use: To mark if intent is already in use for the channel
 * @offset: next write offset (initially 0)
 * @node:	list node
 */
struct glink_core_rx_intent {
	void *data;
	u32 id;
	size_t size;
	bool reuse;
	bool in_use;
	bool advertised;
	u32 offset;

	struct list_head node;
};

/**
 * struct qcom_glink - driver context, relates to one remote subsystem
 * @dev:	reference to the associated struct device
 * @mbox_client: mailbox client
 * @mbox_chan:  mailbox channel
 * @rx_pipe:	pipe object for receive FIFO
 * @tx_pipe:	pipe object for transmit FIFO
 * @irq:	IRQ for signaling incoming events
 * @kworker:	kworker to handle rx_done work
 * @task:	kthread running @kworker
 * @rx_work:	worker for handling received control messages
 * @rx_lock:	protects the @rx_queue
 * @rx_queue:	queue of received control messages to be processed in @rx_work
 * @tx_lock:	synchronizes operations on the tx fifo
 * @idr_lock:	synchronizes @lcids and @rcids modifications
 * @lcids:	idr of all channels with a known local channel id
 * @rcids:	idr of all channels with a known remote channel id
 * @in_reset:	reset status of this edge
 * @features:	remote features
 * @intentless:	flag to indicate that there is no intent
 * @tx_avail_notify: Waitqueue for pending tx tasks
 * @sent_read_notify: flag to check cmd sent or not
 * @ilc:	ipc logging context reference
 */
struct qcom_glink {
	struct device *dev;

	const char *name;

	struct mbox_client mbox_client;
	struct mbox_chan *mbox_chan;

	struct qcom_glink_pipe *rx_pipe;
	struct qcom_glink_pipe *tx_pipe;

	int irq;
	char irqname[GLINK_NAME_SIZE];
	spinlock_t irq_lock;
	bool irq_running;

	struct kthread_worker kworker;
	struct task_struct *task;
	struct cpumask cpu_mask;

	struct work_struct rx_work;
	spinlock_t rx_lock;
	struct list_head rx_queue;

	spinlock_t tx_lock;

	spinlock_t idr_lock;
	struct idr lcids;
	struct idr rcids;

	atomic_t in_reset;
	unsigned long features;

	bool intentless;
	wait_queue_head_t tx_avail_notify;
	bool sent_read_notify;

	void *ilc;
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
 * @intent_lock: lock for protection of @liids, @riids
 * @liids:	idr of all local intents
 * @riids:	idr of all remote intents
 * @intent_work: worker responsible for transmitting rx_done packets
 * @done_intents: list of intents that needs to be announced rx_done
 * @defer_intents: list of intents held by the client released by rpmsg_rx_done
 * @buf:	receive buffer, for gathering fragments
 * @buf_offset:	write offset in @buf
 * @buf_size:	size of current @buf
 * @open_ack:	completed once remote has acked the open-request
 * @open_req:	completed once open-request has been received
 * @intent_req_lock: Synchronises multiple intent requests
 * @intent_req_result: Result of intent request
 * @intent_req_acked: Status of intent request acknowledgment
 * @intent_req_completed: Status of intent request completion
 * @intent_req_ack: Waitqueue for @intent_req_acked
 * @intent_req_comp: Waitqueue for @intent_req_completed
 * @intent_timeout_count: number of times intents have timed out consecutively
 * @local_signals: local side signals
 * @remote_sigalss: remote side signals
 * @signals_cb: client callback for notifying signal change
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

	spinlock_t intent_lock;
	struct idr liids;
	struct idr riids;
	struct kthread_work intent_work;
	struct list_head done_intents;
	struct list_head defer_intents;

	struct glink_core_rx_intent *buf;
	int buf_offset;
	int buf_size;

	struct completion open_ack;
	struct completion open_req;

	struct mutex intent_req_lock;
	bool intent_req_result;
	bool channel_ready;
	atomic_t intent_req_acked;
	atomic_t intent_req_completed;
	wait_queue_head_t intent_req_ack;
	wait_queue_head_t intent_req_comp;
	int intent_timeout_count;

	unsigned int local_signals;
	unsigned int remote_signals;
	int (*signals_cb)(struct rpmsg_device *dev, void *priv, u32 old, u32 new);
};

#define MAX_INTENT_TIMEOUTS		2

#define to_glink_channel(_ept) container_of(_ept, struct glink_channel, ept)

static const struct rpmsg_endpoint_ops glink_endpoint_ops;

#define RPM_CMD_VERSION			0
#define RPM_CMD_VERSION_ACK		1
#define RPM_CMD_OPEN			2
#define RPM_CMD_CLOSE			3
#define RPM_CMD_OPEN_ACK		4
#define RPM_CMD_INTENT			5
#define RPM_CMD_RX_DONE			6
#define RPM_CMD_RX_INTENT_REQ		7
#define RPM_CMD_RX_INTENT_REQ_ACK	8
#define RPM_CMD_TX_DATA			9
#define RPM_CMD_TX_DATA_ZERO_COPY	10
#define RPM_CMD_CLOSE_ACK		11
#define RPM_CMD_TX_DATA_CONT		12
#define RPM_CMD_READ_NOTIF		13
#define RPM_CMD_RX_DONE_W_REUSE		14
#define RPM_CMD_SIGNALS			15

#define GLINK_FEATURE_INTENTLESS	BIT(1)

#define NATIVE_DTR_SIG			BIT(31)
#define NATIVE_CTS_SIG			BIT(30)
#define NATIVE_CD_SIG			BIT(29)
#define NATIVE_RI_SIG			BIT(28)

static void qcom_glink_rx_done_work(struct kthread_work *work);

static struct glink_channel *qcom_glink_alloc_channel(struct qcom_glink *glink,
						      const char *name)
{
	struct glink_channel *channel;

	channel = kzalloc(sizeof(*channel), GFP_KERNEL);
	if (!channel)
		return ERR_PTR(-ENOMEM);

	/* Setup glink internal glink_channel data */
	spin_lock_init(&channel->recv_lock);
	spin_lock_init(&channel->intent_lock);
	mutex_init(&channel->intent_req_lock);

	channel->glink = glink;
	channel->name = kstrdup(name, GFP_KERNEL);

	init_completion(&channel->open_req);
	init_completion(&channel->open_ack);
	atomic_set(&channel->intent_req_acked, 0);
	atomic_set(&channel->intent_req_completed, 0);
	init_waitqueue_head(&channel->intent_req_ack);
	init_waitqueue_head(&channel->intent_req_comp);
	channel->intent_timeout_count = 0;

	INIT_LIST_HEAD(&channel->done_intents);
	INIT_LIST_HEAD(&channel->defer_intents);
	kthread_init_work(&channel->intent_work, qcom_glink_rx_done_work);

	idr_init(&channel->liids);
	idr_init(&channel->riids);
	kref_init(&channel->refcount);

	return channel;
}

static void qcom_glink_channel_release(struct kref *ref)
{
	struct glink_channel *channel = container_of(ref, struct glink_channel,
						     refcount);
	struct glink_core_rx_intent *intent;
	struct glink_core_rx_intent *tmp;
	unsigned long flags;
	int iid;

	CH_INFO(channel, "\n");
	channel->intent_req_result = false;
	atomic_inc(&channel->intent_req_acked);
	wake_up(&channel->intent_req_ack);
	atomic_inc(&channel->intent_req_completed);
	wake_up(&channel->intent_req_comp);

	/* cancel pending rx_done work */
	kthread_cancel_work_sync(&channel->intent_work);

	spin_lock_irqsave(&channel->intent_lock, flags);
	/* Free all non-reuse intents pending rx_done work */
	list_for_each_entry_safe(intent, tmp, &channel->done_intents, node) {
		if (!intent->size)
			intent->data = NULL;

		if (!intent->reuse) {
			kfree(intent->data);
			kfree(intent);
		}
	}
	list_for_each_entry_safe(intent, tmp, &channel->defer_intents, node) {
		if (!intent->size)
			intent->data = NULL;

		if (!intent->reuse) {
			kfree(intent->data);
			kfree(intent);
		}
	}

	idr_for_each_entry(&channel->liids, tmp, iid) {
		kfree(tmp->data);
		kfree(tmp);
	}
	idr_destroy(&channel->liids);

	idr_for_each_entry(&channel->riids, tmp, iid)
		kfree(tmp);
	idr_destroy(&channel->riids);
	spin_unlock_irqrestore(&channel->intent_lock, flags);

	kfree(channel->name);
	kfree(channel);
}

static size_t qcom_glink_rx_avail(struct qcom_glink *glink)
{
	return glink->rx_pipe->avail(glink->rx_pipe);
}

static void qcom_glink_rx_peak(struct qcom_glink *glink,
			       void *data, unsigned int offset, size_t count)
{
	glink->rx_pipe->peak(glink->rx_pipe, data, offset, count);
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

static void qcom_glink_pipe_reset(struct qcom_glink *glink)
{
	if (glink->tx_pipe->reset)
		glink->tx_pipe->reset(glink->tx_pipe);

	if (glink->rx_pipe->reset)
		glink->rx_pipe->reset(glink->rx_pipe);
}

static void qcom_glink_send_read_notify(struct qcom_glink *glink)
{
	struct glink_msg msg;

	msg.cmd = cpu_to_le16(RPM_CMD_READ_NOTIF);
	msg.param1 = 0;
	msg.param2 = 0;

	GLINK_INFO(glink->ilc, "send READ NOTIFY cmd\n");

	qcom_glink_tx_write(glink, &msg, sizeof(msg), NULL, 0);

	mbox_send_message(glink->mbox_chan, NULL);
	mbox_client_txdone(glink->mbox_chan, 0);
}

static int qcom_glink_tx(struct qcom_glink *glink,
			 const void *hdr, size_t hlen,
			 const void *data, size_t dlen, bool wait)
{
	unsigned int tlen = hlen + dlen;
	unsigned long flags;
	int ret = 0;

	/* Reject packets that are too big */
	if (tlen >= glink->tx_pipe->length)
		return -EINVAL;

	if (atomic_read(&glink->in_reset))
		return -ECONNRESET;

	spin_lock_irqsave(&glink->tx_lock, flags);

	while (qcom_glink_tx_avail(glink) < tlen) {
		if (!wait) {
			ret = -EAGAIN;
			goto out;
		}

		if (!glink->sent_read_notify) {
			glink->sent_read_notify = true;
			qcom_glink_send_read_notify(glink);
		}

		/* Wait without holding the tx_lock */
		spin_unlock_irqrestore(&glink->tx_lock, flags);

		wait_event_timeout(glink->tx_avail_notify,
				   qcom_glink_tx_avail(glink) >= tlen ||
				   atomic_read(&glink->in_reset), 10 * HZ);

		spin_lock_irqsave(&glink->tx_lock, flags);

		if (atomic_read(&glink->in_reset)) {
			ret = -ECONNRESET;
			goto out;
		}

		if (qcom_glink_tx_avail(glink) >= tlen)
			glink->sent_read_notify = false;
	}

	qcom_glink_tx_write(glink, hdr, hlen, data, dlen);

	mbox_send_message(glink->mbox_chan, NULL);
	mbox_client_txdone(glink->mbox_chan, 0);

out:
	spin_unlock_irqrestore(&glink->tx_lock, flags);

	return ret;
}

static int qcom_glink_send_version(struct qcom_glink *glink)
{
	struct glink_msg msg;

	msg.cmd = cpu_to_le16(RPM_CMD_VERSION);
	msg.param1 = cpu_to_le16(GLINK_VERSION_1);
	msg.param2 = cpu_to_le32(glink->features);

	GLINK_INFO(glink->ilc, "vers:%d features:%d\n", msg.param1, msg.param2);
	return qcom_glink_tx(glink, &msg, sizeof(msg), NULL, 0, true);
}

static void qcom_glink_send_version_ack(struct qcom_glink *glink)
{
	struct glink_msg msg;

	msg.cmd = cpu_to_le16(RPM_CMD_VERSION_ACK);
	msg.param1 = cpu_to_le16(GLINK_VERSION_1);
	msg.param2 = cpu_to_le32(glink->features);

	GLINK_INFO(glink->ilc, "vers:%d features:%d\n", msg.param1, msg.param2);
	qcom_glink_tx(glink, &msg, sizeof(msg), NULL, 0, true);
}

static void qcom_glink_send_open_ack(struct qcom_glink *glink,
				     struct glink_channel *channel)
{
	struct glink_msg msg;

	msg.cmd = cpu_to_le16(RPM_CMD_OPEN_ACK);
	msg.param1 = cpu_to_le16(channel->rcid);
	msg.param2 = cpu_to_le32(0);

	CH_INFO(channel, "\n");
	qcom_glink_tx(glink, &msg, sizeof(msg), NULL, 0, true);
}

static void qcom_glink_handle_intent_req_ack(struct qcom_glink *glink,
					     unsigned int cid, bool granted)
{
	struct glink_channel *channel;
	unsigned long flags;

	spin_lock_irqsave(&glink->idr_lock, flags);
	channel = idr_find(&glink->rcids, cid);
	spin_unlock_irqrestore(&glink->idr_lock, flags);
	if (!channel) {
		dev_err(glink->dev, "unable to find channel\n");
		return;
	}

	channel->intent_req_result = granted;
	channel->intent_timeout_count = 0;
	atomic_inc(&channel->intent_req_acked);
	wake_up(&channel->intent_req_ack);
	CH_INFO(channel, "\n");
}

/**
 * qcom_glink_send_open_req() - send a RPM_CMD_OPEN request to the remote
 * @glink: Ptr to the glink edge
 * @channel: Ptr to the channel that the open req is sent
 *
 * Allocates a local channel id and sends a RPM_CMD_OPEN message to the remote.
 * Will return with refcount held, regardless of outcome.
 *
 * Return: 0 on success, negative errno otherwise.
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
	CH_INFO(channel, "\n");

	req.msg.cmd = cpu_to_le16(RPM_CMD_OPEN);
	req.msg.param1 = cpu_to_le16(channel->lcid);
	req.msg.param2 = cpu_to_le32(name_len);
	strcpy(req.name, channel->name);

	ret = qcom_glink_tx(glink, &req, req_len, NULL, 0, true);
	if (ret)
		goto remove_idr;

	return 0;

remove_idr:
	CH_INFO(channel, "remote_idr\n");

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

	CH_INFO(channel, "\n");
	qcom_glink_tx(glink, &req, sizeof(req), NULL, 0, true);
}

static void qcom_glink_send_close_ack(struct qcom_glink *glink,
				      unsigned int rcid)
{
	struct glink_msg req;

	req.cmd = cpu_to_le16(RPM_CMD_CLOSE_ACK);
	req.param1 = cpu_to_le16(rcid);
	req.param2 = 0;

	GLINK_INFO(glink->ilc, "rcid:%d\n", rcid);
	qcom_glink_tx(glink, &req, sizeof(req), NULL, 0, true);
}

static int qcom_glink_send_rx_done(struct qcom_glink *glink,
				struct glink_channel *channel,
				struct glink_core_rx_intent *intent,
				bool wait)
{
	struct {
		u16 id;
		u16 lcid;
		u32 liid;
	} __packed cmd;
	unsigned int cid = channel->lcid;
	unsigned int iid = intent->id;
	bool reuse = intent->reuse;
	int ret;

	cmd.id = reuse ? RPM_CMD_RX_DONE_W_REUSE : RPM_CMD_RX_DONE;
	cmd.lcid = cid;
	cmd.liid = iid;

	ret = qcom_glink_tx(glink, &cmd, sizeof(cmd), NULL, 0, wait);
	if (ret)
		return ret;

	/* clear data if zero copy intent */
	if (!intent->size)
		intent->data = NULL;

	if (!reuse) {
		kfree(intent->data);
		kfree(intent);
	}

	CH_INFO(channel, "reuse:%d liid:%d", reuse, iid);
	return 0;
}

static void qcom_glink_rx_done_work(struct kthread_work *work)
{
	struct glink_channel *channel = container_of(work, struct glink_channel,
						     intent_work);
	struct qcom_glink *glink = channel->glink;
	struct glink_core_rx_intent *intent, *tmp;
	unsigned long flags;

	spin_lock_irqsave(&channel->intent_lock, flags);
	list_for_each_entry_safe(intent, tmp, &channel->done_intents, node) {
		list_del(&intent->node);
		spin_unlock_irqrestore(&channel->intent_lock, flags);

		qcom_glink_send_rx_done(glink, channel, intent, true);

		spin_lock_irqsave(&channel->intent_lock, flags);
	}
	spin_unlock_irqrestore(&channel->intent_lock, flags);
}

static void __qcom_glink_rx_done(struct qcom_glink *glink,
			       struct glink_channel *channel,
			       struct glink_core_rx_intent *intent,
			       bool defer)
{
	unsigned long flags;
	int ret = -EAGAIN;

	/* We don't send RX_DONE to intentless systems */
	if (glink->intentless) {
		kfree(intent->data);
		kfree(intent);
		return;
	}

	/* Take it off the tree of receive intents */
	if (!intent->reuse) {
		spin_lock_irqsave(&channel->intent_lock, flags);
		idr_remove(&channel->liids, intent->id);
		spin_unlock_irqrestore(&channel->intent_lock, flags);
	}

	/* Move intent to defer list until client calls rpmsg_rx_done */
	if (defer) {
		spin_lock_irqsave(&channel->intent_lock, flags);
		list_add_tail(&intent->node, &channel->defer_intents);
		spin_unlock_irqrestore(&channel->intent_lock, flags);
		return;
	}

	/* Schedule the sending of a rx_done indication */
	spin_lock_irqsave(&channel->intent_lock, flags);
	if (list_empty(&channel->done_intents))
		ret = qcom_glink_send_rx_done(glink, channel, intent, false);

	if (ret) {
		list_add_tail(&intent->node, &channel->done_intents);
		kthread_queue_work(&glink->kworker, &channel->intent_work);
	}
	spin_unlock_irqrestore(&channel->intent_lock, flags);
}

bool qcom_glink_rx_done_supported(struct rpmsg_endpoint *ept)
{
	struct glink_channel *channel;
	struct qcom_glink *glink;

	if (WARN_ON(!ept))
		return -EINVAL;

	channel = to_glink_channel(ept);
	glink = channel->glink;

	return glink->features & GLINK_FEATURE_ZERO_COPY;
}
EXPORT_SYMBOL(qcom_glink_rx_done_supported);

/**
 * rpmsg_rx_done() - release resources related to @data from a @rx_cb
 * @ept:	the rpmsg endpoint
 * @data:	payload from a message
 *
 * Returns 0 on success and an appropriate error value on failure.
 */
int qcom_glink_rx_done(struct rpmsg_endpoint *ept, void *data)
{
	struct glink_core_rx_intent *intent, *tmp;
	struct glink_channel *channel;
	struct qcom_glink *glink;
	unsigned long flags;

	if (WARN_ON(!ept))
		return -EINVAL;

	if (!qcom_glink_rx_done_supported(ept))
		return -EINVAL;

	channel = to_glink_channel(ept);
	glink = channel->glink;

	spin_lock_irqsave(&channel->intent_lock, flags);
	list_for_each_entry_safe(intent, tmp, &channel->defer_intents, node) {
		if (intent->data == data) {
			list_del(&intent->node);
			spin_unlock_irqrestore(&channel->intent_lock, flags);

			qcom_glink_send_rx_done(glink, channel, intent, true);
			return 0;
		}
	}
	spin_unlock_irqrestore(&channel->intent_lock, flags);

	return -EINVAL;
}
EXPORT_SYMBOL(qcom_glink_rx_done);

/**
 * qcom_glink_receive_version() - receive version/features from remote system
 *
 * @glink:	pointer to transport interface
 * @version:	remote version
 * @features:	remote features
 *
 * This function is called in response to a remote-initiated version/feature
 * negotiation sequence.
 */
static void qcom_glink_receive_version(struct qcom_glink *glink,
				       u32 version,
				       u32 features)
{
	GLINK_INFO(glink->ilc, "vers:%d features:%d\n", version, features);

	switch (version) {
	case 0:
		break;
	case GLINK_VERSION_1:
		glink->features &= features;
		fallthrough;
	default:
		qcom_glink_send_version_ack(glink);
		break;
	}
}

/**
 * qcom_glink_receive_version_ack() - receive negotiation ack from remote system
 *
 * @glink:	pointer to transport interface
 * @version:	remote version response
 * @features:	remote features response
 *
 * This function is called in response to a local-initiated version/feature
 * negotiation sequence and is the counter-offer from the remote side based
 * upon the initial version and feature set requested.
 */
static void qcom_glink_receive_version_ack(struct qcom_glink *glink,
					   u32 version,
					   u32 features)
{
	GLINK_INFO(glink->ilc, "vers:%d features:%d\n", version, features);

	switch (version) {
	case 0:
		/* Version negotiation failed */
		break;
	case GLINK_VERSION_1:
		if (features == glink->features)
			break;

		glink->features &= features;
		fallthrough;
	default:
		qcom_glink_send_version(glink);
		break;
	}
}

/**
 * qcom_glink_send_intent_req_ack() - convert an rx intent request ack cmd to
 * 	wire format and transmit
 * @glink:	The transport to transmit on.
 * @channel:	The glink channel
 * @granted:	The request response to encode.
 *
 * Return: 0 on success or standard Linux error code.
 */
static int qcom_glink_send_intent_req_ack(struct qcom_glink *glink,
					  struct glink_channel *channel,
					  bool granted)
{
	struct glink_msg msg;

	msg.cmd = cpu_to_le16(RPM_CMD_RX_INTENT_REQ_ACK);
	msg.param1 = cpu_to_le16(channel->lcid);
	msg.param2 = cpu_to_le32(granted);

	CH_INFO(channel, "\n");
	qcom_glink_tx(glink, &msg, sizeof(msg), NULL, 0, true);

	return 0;
}

/**
 * qcom_glink_advertise_intent - convert an rx intent cmd to wire format and
 *			   transmit
 * @glink:	The transport to transmit on.
 * @channel:	The local channel
 * @intent:	The intent to pass on to remote.
 *
 * Return: 0 on success or standard Linux error code.
 */
static int qcom_glink_advertise_intent(struct qcom_glink *glink,
				       struct glink_channel *channel,
				       struct glink_core_rx_intent *intent)
{
	struct command {
		__le16 id;
		__le16 lcid;
		__le32 count;
		__le32 size;
		__le32 liid;
	} __packed;
	struct command cmd;
	unsigned long flags;

	spin_lock_irqsave(&channel->intent_lock, flags);
	if (intent->advertised) {
		spin_unlock_irqrestore(&channel->intent_lock, flags);
		return 0;
	}
	intent->advertised = true;
	spin_unlock_irqrestore(&channel->intent_lock, flags);

	cmd.id = cpu_to_le16(RPM_CMD_INTENT);
	cmd.lcid = cpu_to_le16(channel->lcid);
	cmd.count = cpu_to_le32(1);
	cmd.size = cpu_to_le32(intent->size);
	cmd.liid = cpu_to_le32(intent->id);

	CH_INFO(channel, "count:%d size:%zd liid:%d\n", 1,
		intent->size, intent->id);

	qcom_glink_tx(glink, &cmd, sizeof(cmd), NULL, 0, true);

	return 0;
}

static struct glink_core_rx_intent *
qcom_glink_alloc_intent(struct qcom_glink *glink,
			struct glink_channel *channel,
			size_t size,
			bool reuseable)
{
	struct glink_core_rx_intent *intent;
	int ret;
	unsigned long flags;

	intent = kzalloc(sizeof(*intent), GFP_KERNEL);
	if (!intent)
		return NULL;

	if (size) {
		intent->data = kzalloc(size, GFP_KERNEL);
		if (!intent->data)
			goto free_intent;
	}

	spin_lock_irqsave(&channel->intent_lock, flags);
	ret = idr_alloc_cyclic(&channel->liids, intent, 1, -1, GFP_ATOMIC);
	if (ret < 0) {
		spin_unlock_irqrestore(&channel->intent_lock, flags);
		goto free_data;
	}
	spin_unlock_irqrestore(&channel->intent_lock, flags);

	intent->id = ret;
	intent->size = size;
	intent->reuse = reuseable;

	return intent;

free_data:
	kfree(intent->data);
free_intent:
	kfree(intent);
	return NULL;
}

static void qcom_glink_handle_rx_done(struct qcom_glink *glink,
				      u32 cid, uint32_t iid,
				      bool reuse)
{
	struct glink_core_rx_intent *intent;
	struct glink_channel *channel;
	unsigned long flags;

	spin_lock_irqsave(&glink->idr_lock, flags);
	channel = idr_find(&glink->rcids, cid);
	spin_unlock_irqrestore(&glink->idr_lock, flags);
	if (!channel) {
		dev_err(glink->dev, "invalid channel id received\n");
		return;
	}

	spin_lock_irqsave(&channel->intent_lock, flags);
	intent = idr_find(&channel->riids, iid);

	if (!intent) {
		spin_unlock_irqrestore(&channel->intent_lock, flags);
		dev_err(glink->dev, "invalid intent id received\n");
		return;
	}

	intent->in_use = false;
	CH_INFO(channel, "reuse:%d iid:%d\n", reuse, intent->id);

	if (!reuse) {
		idr_remove(&channel->riids, intent->id);
		kfree(intent);
	}
	spin_unlock_irqrestore(&channel->intent_lock, flags);
}

/**
 * qcom_glink_handle_intent_req() - Receive a request for rx_intent
 *					    from remote side
 * @glink:      Pointer to the transport interface
 * @cid:	Remote channel ID
 * @size:	size of the intent
 *
 * The function searches for the local channel to which the request for
 * rx_intent has arrived and allocates and notifies the remote back
 */
static void qcom_glink_handle_intent_req(struct qcom_glink *glink,
					 u32 cid, size_t size)
{
	struct glink_core_rx_intent *intent = NULL;
	struct glink_core_rx_intent *tmp;
	struct glink_channel *channel;
	struct rpmsg_endpoint *ept;
	unsigned long flags;
	int iid;

	spin_lock_irqsave(&glink->idr_lock, flags);
	channel = idr_find(&glink->rcids, cid);
	spin_unlock_irqrestore(&glink->idr_lock, flags);

	if (!channel) {
		pr_err("%s channel not found for cid %u\n", __func__, cid);
		return;
	}

	spin_lock_irqsave(&channel->intent_lock, flags);
	idr_for_each_entry(&channel->liids, tmp, iid) {
		if (tmp->size >= size && tmp->reuse) {
			intent = tmp;
			break;
		}
	}
	spin_unlock_irqrestore(&channel->intent_lock, flags);
	if (intent) {
		qcom_glink_send_intent_req_ack(glink, channel, !!intent);
		return;
	}

	ept = &channel->ept;
	intent = qcom_glink_alloc_intent(glink, channel, size, false);
	if (intent && channel->channel_ready)
		qcom_glink_advertise_intent(glink, channel, intent);

	qcom_glink_send_intent_req_ack(glink, channel, !!intent);
}

static int qcom_glink_rx_defer(struct qcom_glink *glink, size_t extra)
{
	struct glink_defer_cmd *dcmd;

	extra = ALIGN(extra, 8);

	if (qcom_glink_rx_avail(glink) < sizeof(struct glink_msg) + extra) {
		dev_dbg(glink->dev, "Insufficient data in rx fifo");
		return -ENXIO;
	}

	dcmd = kzalloc(struct_size(dcmd, data, extra), GFP_ATOMIC);
	if (!dcmd)
		return -ENOMEM;

	INIT_LIST_HEAD(&dcmd->node);

	qcom_glink_rx_peak(glink, &dcmd->msg, 0, sizeof(dcmd->msg) + extra);

	spin_lock(&glink->rx_lock);
	list_add_tail(&dcmd->node, &glink->rx_queue);
	spin_unlock(&glink->rx_lock);

	schedule_work(&glink->rx_work);
	qcom_glink_rx_advance(glink, sizeof(dcmd->msg) + extra);

	return 0;
}

bool qcom_glink_is_wakeup(bool reset)
{
	if (!glink_resume_pkt)
		return false;

	if (reset)
		glink_resume_pkt = false;

	return true;
}
EXPORT_SYMBOL(qcom_glink_is_wakeup);

static int qcom_glink_rx_data(struct qcom_glink *glink, size_t avail)
{
	struct glink_core_rx_intent *intent;
	struct glink_channel *channel;
	struct {
		struct glink_msg msg;
		__le32 chunk_size;
		__le32 left_size;
	} __packed hdr;
	unsigned int chunk_size;
	unsigned int left_size;
	bool rx_done_defer;
	unsigned int rcid;
	unsigned int liid;
	int ret = 0;
	unsigned long flags;

	if (avail < sizeof(hdr)) {
		dev_dbg(glink->dev, "Not enough data in fifo\n");
		return -EAGAIN;
	}

	qcom_glink_rx_peak(glink, &hdr, 0, sizeof(hdr));
	chunk_size = le32_to_cpu(hdr.chunk_size);
	left_size = le32_to_cpu(hdr.left_size);

	if (avail < sizeof(hdr) + chunk_size) {
		dev_dbg(glink->dev, "Payload not yet in fifo\n");
		return -EAGAIN;
	}

	rcid = le16_to_cpu(hdr.msg.param1);
	spin_lock_irqsave(&glink->idr_lock, flags);
	channel = idr_find(&glink->rcids, rcid);
	spin_unlock_irqrestore(&glink->idr_lock, flags);
	if (!channel) {
		dev_dbg(glink->dev, "Data on non-existing channel\n");

		/* Drop the message */
		goto advance_rx;
	}
	CH_INFO(channel, "chunk_size:%d left_size:%d\n", chunk_size, left_size);

	if (glink->intentless) {
		/* Might have an ongoing, fragmented, message to append */
		if (!channel->buf) {
			intent = kzalloc(sizeof(*intent), GFP_ATOMIC);
			if (!intent)
				return -ENOMEM;

			intent->data = kmalloc(chunk_size + left_size,
					       GFP_ATOMIC);
			if (!intent->data) {
				kfree(intent);
				return -ENOMEM;
			}

			intent->id = 0xbabababa;
			intent->size = chunk_size + left_size;
			intent->offset = 0;

			channel->buf = intent;
		} else {
			intent = channel->buf;
		}
	} else {
		liid = le32_to_cpu(hdr.msg.param2);

		spin_lock_irqsave(&channel->intent_lock, flags);
		intent = idr_find(&channel->liids, liid);
		spin_unlock_irqrestore(&channel->intent_lock, flags);

		if (!intent) {
			dev_err(glink->dev,
				"no intent found for channel %s intent %d",
				channel->name, liid);
			ret = -ENOENT;
			goto advance_rx;
		}
	}

	if (intent->size - intent->offset < chunk_size) {
		dev_err(glink->dev, "Insufficient space in intent\n");

		/* The packet header lied, drop payload */
		goto advance_rx;
	}

	qcom_glink_rx_peak(glink, intent->data + intent->offset,
			   sizeof(hdr), chunk_size);
	intent->offset += chunk_size;

	/* Handle message when no fragments remain to be received */
	if (!left_size) {
		spin_lock(&channel->recv_lock);
		if (channel->ept.cb) {
			ret = channel->ept.cb(channel->ept.rpdev,
					intent->data,
					intent->offset,
					channel->ept.priv,
					RPMSG_ADDR_ANY);

			if (ret < 0 && ret != -ENODEV) {
				CH_ERR(channel,
					"callback error ret = %d\n", ret);
				ret = 0;
			}
		} else {
			CH_ERR(channel, "callback not present\n");
		}
		spin_unlock(&channel->recv_lock);

		if (qcom_glink_is_wakeup(true)) {
			pr_info("%s[%d:%d] %s: wakeup packet size:%d\n",
				channel->name, channel->lcid, channel->rcid,
				__func__, intent->offset);
		}

		intent->offset = 0;
		channel->buf = NULL;


		if (qcom_glink_rx_done_supported(&channel->ept) && ret == RPMSG_DEFER)
			rx_done_defer = true;
		else
			rx_done_defer = false;

		__qcom_glink_rx_done(glink, channel, intent, rx_done_defer);
	}

advance_rx:
	qcom_glink_rx_advance(glink, ALIGN(sizeof(hdr) + chunk_size, 8));

	return ret;
}

static int qcom_glink_rx_data_zero_copy(struct qcom_glink *glink, size_t avail)
{
	struct glink_core_rx_intent *intent;
	struct glink_channel *channel;
	struct {
		struct glink_msg msg;
		__le32 pool_id;
		__le32 size;
		__le64 addr;
	} __packed hdr;
	unsigned long flags;
	bool rx_done_defer;
	unsigned int rcid;
	unsigned int liid;
	unsigned int len;
	int ret = 0;
	void *data;
	u64 da;

	if (avail < sizeof(hdr)) {
		dev_dbg(glink->dev, "Not enough data in fifo\n");
		return -EAGAIN;
	}
	qcom_glink_rx_peak(glink, &hdr, 0, sizeof(hdr));

	if (glink->intentless) {
		dev_dbg(glink->dev, "Zero copy cannot be intentless\n");
		goto advance_rx;
	}

	rcid = le16_to_cpu(hdr.msg.param1);
	spin_lock_irqsave(&glink->idr_lock, flags);
	channel = idr_find(&glink->rcids, rcid);
	spin_unlock_irqrestore(&glink->idr_lock, flags);
	if (!channel) {
		dev_dbg(glink->dev, "Data on non-existing channel\n");
		goto advance_rx;
	}

	liid = le32_to_cpu(hdr.msg.param2);
	spin_lock_irqsave(&channel->intent_lock, flags);
	intent = idr_find(&channel->liids, liid);
	spin_unlock_irqrestore(&channel->intent_lock, flags);
	if (!intent) {
		CH_ERR(channel, "no intent found liid:%d\n", liid);
		ret = -ENOENT;
		goto advance_rx;
	}
	if (intent->size) {
		CH_ERR(channel, "zero copy req wrong intent liid:%d size:%d\n",
		       liid, intent->size);
		goto advance_rx;
	}

	/* Only process the first vector in the array */
	da = le64_to_cpu(hdr.addr);
	len = le32_to_cpu(hdr.size);
	data = qcom_glink_prepare_da_for_cpu(da, len);
	if (!data) {
		CH_ERR(channel, "failed to get va da:0x%llx len:%d\n", da, len);
		goto advance_rx;
	}
	CH_INFO(channel, "da:0x%llx va:0x%llx len:%d\n", da, data, len);

	intent->data = data;
	intent->offset = len;
	spin_lock(&channel->recv_lock);
	if (channel->ept.cb) {
		ret = channel->ept.cb(channel->ept.rpdev, intent->data,
				intent->offset,
				channel->ept.priv,
				RPMSG_ADDR_ANY);

		if (ret < 0 && ret != -ENODEV) {
			CH_ERR(channel,
				"callback error ret = %d\n", ret);
			ret = 0;
		}
	} else {
		CH_ERR(channel, "callback not present\n");
	}
	spin_unlock(&channel->recv_lock);

	if (qcom_glink_is_wakeup(true)) {
		pr_info("%s[%d:%d] %s: wakeup packet size:%d\n", channel->name,
			channel->lcid, channel->rcid,
			__func__, intent->offset);
	}
	intent->offset = 0;

	if (qcom_glink_rx_done_supported(&channel->ept) && ret == RPMSG_DEFER)
		rx_done_defer = true;
	else
		rx_done_defer = false;

	__qcom_glink_rx_done(glink, channel, intent, rx_done_defer);

advance_rx:
	qcom_glink_rx_advance(glink, ALIGN(sizeof(hdr), 8));

	return 0;
}

static void qcom_glink_handle_intent(struct qcom_glink *glink,
				     unsigned int cid,
				     unsigned int count,
				     size_t avail)
{
	struct glink_core_rx_intent *intent;
	struct glink_channel *channel;
	struct intent_pair {
		__le32 size;
		__le32 iid;
	};

	struct {
		struct glink_msg msg;
		struct intent_pair intents[];
	} __packed * msg;

	const size_t msglen = struct_size(msg, intents, count);
	int ret;
	int i;
	unsigned long flags;

	if (avail < msglen) {
		dev_dbg(glink->dev, "Not enough data in fifo\n");
		return;
	}

	spin_lock_irqsave(&glink->idr_lock, flags);
	channel = idr_find(&glink->rcids, cid);
	spin_unlock_irqrestore(&glink->idr_lock, flags);
	if (!channel) {
		dev_err(glink->dev, "intents for non-existing channel\n");
		qcom_glink_rx_advance(glink, ALIGN(msglen, 8));
		return;
	}

	msg = kmalloc(msglen, GFP_ATOMIC);
	if (!msg)
		return;

	qcom_glink_rx_peak(glink, msg, 0, msglen);

	for (i = 0; i < count; ++i) {
		intent = kzalloc(sizeof(*intent), GFP_ATOMIC);
		if (!intent)
			break;

		intent->id = le32_to_cpu(msg->intents[i].iid);
		intent->size = le32_to_cpu(msg->intents[i].size);

		CH_INFO(channel, "riid:%d size:%zd\n",
			intent->id, intent->size);

		spin_lock_irqsave(&channel->intent_lock, flags);
		ret = idr_alloc(&channel->riids, intent,
				intent->id, intent->id + 1, GFP_ATOMIC);
		spin_unlock_irqrestore(&channel->intent_lock, flags);

		if (ret < 0)
			dev_err(glink->dev, "failed to store remote intent\n");
	}

	atomic_inc(&channel->intent_req_completed);
	wake_up(&channel->intent_req_comp);

	kfree(msg);
	qcom_glink_rx_advance(glink, ALIGN(msglen, 8));
}

static int qcom_glink_rx_open_ack(struct qcom_glink *glink, unsigned int lcid)
{
	struct glink_channel *channel;

	spin_lock(&glink->idr_lock);
	channel = idr_find(&glink->lcids, lcid);
	spin_unlock(&glink->idr_lock);
	if (!channel) {
		dev_err(glink->dev, "Invalid open ack packet\n");
		return -EINVAL;
	}

	CH_INFO(channel, "\n");
	complete_all(&channel->open_ack);

	return 0;
}

/**
 * qcom_glink_send_signals() - convert a signal cmd to wire format and transmit
 * @glink:	The transport to transmit on.
 * @channel:	The glink channel
 * @signals:	The signals to encode.
 *
 * Return: 0 on success or standard Linux error code.
 */
static int qcom_glink_send_signals(struct qcom_glink *glink,
				   struct glink_channel *channel,
				   u32 signals)
{
	struct glink_msg msg;

	/* convert signals from TIOCM to NATIVE */
	signals &= 0x0fff;
	if (signals & TIOCM_DTR)
		signals |= NATIVE_DTR_SIG;
	if (signals & TIOCM_RTS)
		signals |= NATIVE_CTS_SIG;
	if (signals & TIOCM_CD)
		signals |= NATIVE_CD_SIG;
	if (signals & TIOCM_RI)
		signals |= NATIVE_RI_SIG;

	msg.cmd = cpu_to_le16(RPM_CMD_SIGNALS);
	msg.param1 = cpu_to_le16(channel->lcid);
	msg.param2 = cpu_to_le32(signals);

	GLINK_INFO(glink->ilc, "signals:%d\n", signals);
	return qcom_glink_tx(glink, &msg, sizeof(msg), NULL, 0, true);
}

static int qcom_glink_handle_signals(struct qcom_glink *glink,
				     unsigned int rcid, unsigned int signals)
{
	struct glink_channel *channel;
	unsigned long flags;
	u32 old;

	spin_lock_irqsave(&glink->idr_lock, flags);
	channel = idr_find(&glink->rcids, rcid);
	spin_unlock_irqrestore(&glink->idr_lock, flags);
	if (!channel) {
		dev_err(glink->dev, "signal for non-existing channel\n");
		return -EINVAL;
	}

	old = channel->remote_signals;

	/* convert signals from NATIVE to TIOCM */
	if (signals & NATIVE_DTR_SIG)
		signals |= TIOCM_DSR;
	if (signals & NATIVE_CTS_SIG)
		signals |= TIOCM_CTS;
	if (signals & NATIVE_CD_SIG)
		signals |= TIOCM_CD;
	if (signals & NATIVE_RI_SIG)
		signals |= TIOCM_RI;
	signals &= 0x0fff;

	channel->remote_signals = signals;

	CH_INFO(channel, "old:%d new:%d\n", old, channel->remote_signals);
	if (channel->signals_cb)
		channel->signals_cb(channel->ept.rpdev, channel->ept.priv,
				    old, channel->remote_signals);

	return 0;
}

static int qcom_glink_native_rx(struct qcom_glink *glink, int iterations)
{
	struct glink_msg msg;
	unsigned long flags;
	unsigned int param1;
	unsigned int param2;
	unsigned int avail;
	unsigned int cmd;
	int ret = 0;
	int i;

	if (should_wake) {
		pr_info("%s: wakeup %s\n", __func__, glink->irqname);
		glink_resume_pkt = true;
		should_wake = false;
		pm_system_wakeup();
	}

	spin_lock_irqsave(&glink->irq_lock, flags);
	if (glink->irq_running) {
		spin_unlock_irqrestore(&glink->irq_lock, flags);
		return 0;
	}
	glink->irq_running = true;
	spin_unlock_irqrestore(&glink->irq_lock, flags);

	/* To wakeup any blocking writers */
	wake_up_all(&glink->tx_avail_notify);

	for (i = 0; i < iterations || !iterations; i++) {
		avail = qcom_glink_rx_avail(glink);
		if (avail < sizeof(msg))
			break;

		qcom_glink_rx_peak(glink, &msg, 0, sizeof(msg));

		cmd = le16_to_cpu(msg.cmd);
		param1 = le16_to_cpu(msg.param1);
		param2 = le32_to_cpu(msg.param2);

		switch (cmd) {
		case RPM_CMD_VERSION:
		case RPM_CMD_VERSION_ACK:
		case RPM_CMD_CLOSE:
		case RPM_CMD_CLOSE_ACK:
		case RPM_CMD_RX_INTENT_REQ:
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
		case RPM_CMD_TX_DATA_ZERO_COPY:
			ret = qcom_glink_rx_data_zero_copy(glink, avail);
			break;
		case RPM_CMD_READ_NOTIF:
			qcom_glink_rx_advance(glink, ALIGN(sizeof(msg), 8));

			mbox_send_message(glink->mbox_chan, NULL);
			mbox_client_txdone(glink->mbox_chan, 0);
			break;
		case RPM_CMD_INTENT:
			qcom_glink_handle_intent(glink, param1, param2, avail);
			break;
		case RPM_CMD_RX_DONE:
			qcom_glink_handle_rx_done(glink, param1, param2, false);
			qcom_glink_rx_advance(glink, ALIGN(sizeof(msg), 8));
			break;
		case RPM_CMD_RX_DONE_W_REUSE:
			qcom_glink_handle_rx_done(glink, param1, param2, true);
			qcom_glink_rx_advance(glink, ALIGN(sizeof(msg), 8));
			break;
		case RPM_CMD_RX_INTENT_REQ_ACK:
			qcom_glink_handle_intent_req_ack(glink, param1, param2);
			qcom_glink_rx_advance(glink, ALIGN(sizeof(msg), 8));
			break;
		case RPM_CMD_SIGNALS:
			qcom_glink_handle_signals(glink, param1, param2);
			qcom_glink_rx_advance(glink, ALIGN(sizeof(msg), 8));
			break;
		default:
			dev_err(glink->dev, "unhandled rx cmd: %d\n", cmd);
			ret = -EINVAL;
			break;
		}

		if (ret)
			break;
	}

	spin_lock_irqsave(&glink->irq_lock, flags);
	glink->irq_running = false;
	spin_unlock_irqrestore(&glink->irq_lock, flags);

	return qcom_glink_rx_avail(glink);
}

static irqreturn_t qcom_glink_native_intr(int irq, void *data)
{
	struct qcom_glink *glink = data;
	int ret;

	ret = qcom_glink_native_rx(glink, 10);

	return (ret) ? IRQ_WAKE_THREAD : IRQ_HANDLED;
}

static irqreturn_t qcom_glink_native_thread_intr(int irq, void *data)
{
	struct qcom_glink *glink = data;

	qcom_glink_native_rx(glink, 0);

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

	CH_INFO(channel, "\n");
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
	CH_INFO(channel, "err_timeout\n");

	/* qcom_glink_send_open_req() did register the channel in lcids*/
	spin_lock_irqsave(&glink->idr_lock, flags);
	idr_remove(&glink->lcids, channel->lcid);
	spin_unlock_irqrestore(&glink->idr_lock, flags);

release_channel:
	CH_INFO(channel, "release_channel\n");
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

	CH_INFO(channel, "\n");

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
	CH_INFO(channel, "close_link %d\n", ret);

	/*
	 * Send a close request to "undo" our open-ack. The close-ack will
	 * release qcom_glink_send_open_req() reference and the last reference
	 * will be relesed after receiving remote_close or transport unregister
	 * by calling qcom_glink_native_remove().
	 */
	qcom_glink_send_close_req(glink, channel);

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
	CH_INFO(channel, "Initializing ept\n");

	ept = &channel->ept;
	ept->rpdev = rpdev;
	ept->cb = cb;
	ept->priv = priv;
	ept->ops = &glink_endpoint_ops;
	CH_INFO(channel, "Initialized ept\n");

	return ept;
}

static int qcom_glink_announce_create(struct rpmsg_device *rpdev)
{
	struct glink_channel *channel = to_glink_channel(rpdev->ept);
	struct device_node *np = rpdev->dev.of_node;
	struct qcom_glink *glink = channel->glink;
	struct glink_core_rx_intent *intent;
	struct glink_core_rx_intent *tmp;
	const struct property *prop = NULL;
	__be32 defaults[] = { cpu_to_be32(0), cpu_to_be32(2),
			      cpu_to_be32(SZ_1K), cpu_to_be32(5) };
	int num_intents;
	int num_groups = 2;
	__be32 *val = defaults;
	unsigned long flags;
	int iid;
	int size;

	CH_INFO(channel, "Entered\n");
	if (glink->intentless || !completion_done(&channel->open_ack))
		return 0;

	channel->channel_ready = true;

	/*Serve any pending intent request*/
	spin_lock_irqsave(&channel->intent_lock, flags);
	idr_for_each_entry(&channel->liids, tmp, iid) {
		if (!tmp->reuse && !tmp->advertised) {
			intent = tmp;
			spin_unlock_irqrestore(&channel->intent_lock, flags);
			qcom_glink_advertise_intent(glink, channel, intent);
			spin_lock_irqsave(&channel->intent_lock, flags);
		}
	}
	spin_unlock_irqrestore(&channel->intent_lock, flags);

	prop = of_find_property(np, "qcom,intents", NULL);
	if (prop) {
		val = prop->value;
		num_groups = prop->length / sizeof(u32) / 2;
	}

	/* Channel is now open, advertise base set of intents */
	while (num_groups--) {
		size = be32_to_cpup(val++);
		num_intents = be32_to_cpup(val++);
		while (num_intents--) {
			intent = qcom_glink_alloc_intent(glink, channel, size,
							 true);
			if (!intent)
				break;

			qcom_glink_advertise_intent(glink, channel, intent);
		}
	}
	CH_INFO(channel, "Exit\n");
	return 0;
}

static void qcom_glink_destroy_ept(struct rpmsg_endpoint *ept)
{
	struct glink_channel *channel = to_glink_channel(ept);
	struct qcom_glink *glink = channel->glink;
	unsigned long flags;

	spin_lock_irqsave(&channel->recv_lock, flags);
	if (!channel->ept.cb) {
		spin_unlock_irqrestore(&channel->recv_lock, flags);
		return;
	}
	channel->ept.cb = NULL;
	spin_unlock_irqrestore(&channel->recv_lock, flags);

	/* Decouple the potential rpdev from the channel */
	channel->rpdev = NULL;

	qcom_glink_send_close_req(glink, channel);
}

static int qcom_glink_request_intent(struct qcom_glink *glink,
				     struct glink_channel *channel,
				     size_t size)
{
	struct {
		u16 id;
		u16 cid;
		u32 size;
	} __packed cmd;

	int ret;

	mutex_lock(&channel->intent_req_lock);

	atomic_set(&channel->intent_req_acked, 0);
	atomic_set(&channel->intent_req_completed, 0);

	cmd.id = RPM_CMD_RX_INTENT_REQ;
	cmd.cid = channel->lcid;
	cmd.size = size;

	CH_INFO(channel, "size:%zd\n", size);

	ret = qcom_glink_tx(glink, &cmd, sizeof(cmd), NULL, 0, true);
	if (ret)
		goto unlock;

	ret = wait_event_timeout(channel->intent_req_ack,
				 atomic_read(&channel->intent_req_acked) ||
				 atomic_read(&glink->in_reset), 10 * HZ);
	if (!ret) {
		dev_err(glink->dev, "%s: intent request ack timed out (%d)\n",
			channel->name, channel->intent_timeout_count);
		ret = -ETIMEDOUT;
		channel->intent_timeout_count++;
		if (channel->intent_timeout_count >= MAX_INTENT_TIMEOUTS)
			GLINK_BUG(glink->ilc,
				"remoteproc:%s channel:%s unresponsive\n",
				glink->name, channel->name);
	} else if (atomic_read(&glink->in_reset)) {
		CH_INFO(channel, "ssr detected\n");
		ret = -ECONNRESET;
	} else {
		ret = channel->intent_req_result ? 0 : -ECANCELED;
	}

unlock:
	mutex_unlock(&channel->intent_req_lock);
	return ret;
}

static int __qcom_glink_send(struct glink_channel *channel,
			     void *data, int len, bool wait)
{
	struct qcom_glink *glink = channel->glink;
	struct glink_core_rx_intent *intent = NULL;
	struct glink_core_rx_intent *tmp;
	int iid = 0;
	struct {
		struct glink_msg msg;
		__le32 chunk_size;
		__le32 left_size;
	} __packed req;
	int ret;
	unsigned long flags;
	int chunk_size = len;
	int left_size = 0;

	if (!glink->intentless) {
		while (!intent) {
			spin_lock_irqsave(&channel->intent_lock, flags);
			idr_for_each_entry(&channel->riids, tmp, iid) {
				if (tmp->size >= len && !tmp->in_use) {
					if (!intent)
						intent = tmp;
					else if (intent->size > tmp->size)
						intent = tmp;
					if (intent->size == len)
						break;
				}
			}
			if (intent)
				intent->in_use = true;
			spin_unlock_irqrestore(&channel->intent_lock, flags);

			/* We found an available intent */
			if (intent)
				break;

			if (atomic_read(&glink->in_reset))
				return -ECONNRESET;

			if (!wait)
				return -EBUSY;

			ret = qcom_glink_request_intent(glink, channel, len);
			if (ret < 0)
				return ret;

			/*Wait for intents to arrive*/
			ret = wait_event_timeout(channel->intent_req_comp,
				atomic_read(&channel->intent_req_completed) ||
				atomic_read(&glink->in_reset), 10 * HZ);

			if (!ret) {
				dev_err(glink->dev,
					"intent request completion timed out\n");
				ret = -ETIMEDOUT;
			} else if (atomic_read(&glink->in_reset)) {
				CH_INFO(channel, "ssr detected\n");
				ret = -ECONNRESET;
			} else {
				ret = channel->intent_req_result ? 0 : -ECANCELED;
			}

			if (ret < 0)
				return ret;
		}

		iid = intent->id;
	}

	if (wait && chunk_size > SZ_8K) {
		chunk_size = SZ_8K;
		left_size = len - chunk_size;
	}
	req.msg.cmd = cpu_to_le16(RPM_CMD_TX_DATA);
	req.msg.param1 = cpu_to_le16(channel->lcid);
	req.msg.param2 = cpu_to_le32(iid);
	req.chunk_size = cpu_to_le32(chunk_size);
	req.left_size = cpu_to_le32(left_size);

	CH_INFO(channel, "iid:%d chunk_size:%d left_size:%d\n", iid,
		chunk_size, left_size);
	ret = qcom_glink_tx(glink, &req, sizeof(req), data, chunk_size, wait);

	/* Mark intent available if we failed */
	if (ret && intent) {
		intent->in_use = false;
		return ret;
	}

	while (left_size > 0) {
		data = (void *)((char *)data + chunk_size);
		chunk_size = left_size;
		if (chunk_size > SZ_8K)
			chunk_size = SZ_8K;
		left_size -= chunk_size;

		req.msg.cmd = cpu_to_le16(RPM_CMD_TX_DATA_CONT);
		req.msg.param1 = cpu_to_le16(channel->lcid);
		req.msg.param2 = cpu_to_le32(iid);
		req.chunk_size = cpu_to_le32(chunk_size);
		req.left_size = cpu_to_le32(left_size);

		CH_INFO(channel, "iid:%d chunk_size:%d left_size:%d\n", iid,
			chunk_size, left_size);
		ret = qcom_glink_tx(glink, &req, sizeof(req), data,
				    chunk_size, wait);

		/* Mark intent available if we failed */
		if (ret && intent) {
			intent->in_use = false;
			break;
		}
	}
	return ret;
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

static int qcom_glink_sendto(struct rpmsg_endpoint *ept, void *data, int len, u32 dst)
{
	struct glink_channel *channel = to_glink_channel(ept);

	return __qcom_glink_send(channel, data, len, true);
}

static int qcom_glink_trysendto(struct rpmsg_endpoint *ept, void *data, int len, u32 dst)
{
	struct glink_channel *channel = to_glink_channel(ept);

	return __qcom_glink_send(channel, data, len, false);
}

int qcom_glink_get_signals(struct rpmsg_endpoint *ept)
{
	struct glink_channel *channel;

	if (!ept)
		return 0;

	channel = to_glink_channel(ept);

	return channel->remote_signals;
}
EXPORT_SYMBOL(qcom_glink_get_signals);

int qcom_glink_set_signals(struct rpmsg_endpoint *ept, u32 set, u32 clear)
{
	struct glink_channel *channel;
	struct qcom_glink *glink;
	u32 signals;

	if (!ept)
		return -EINVAL;

	channel = to_glink_channel(ept);
	glink = channel->glink;
	signals = channel->local_signals;

	if (set & TIOCM_DTR)
		signals |= TIOCM_DTR;
	if (set & TIOCM_RTS)
		signals |= TIOCM_RTS;
	if (set & TIOCM_CD)
		signals |= TIOCM_CD;
	if (set & TIOCM_RI)
		signals |= TIOCM_RI;
	if (clear & TIOCM_DTR)
		signals &= ~TIOCM_DTR;
	if (clear & TIOCM_RTS)
		signals &= ~TIOCM_RTS;
	if (clear & TIOCM_CD)
		signals &= ~TIOCM_CD;
	if (clear & TIOCM_RI)
		signals &= ~TIOCM_RI;

	channel->local_signals = signals;

	return qcom_glink_send_signals(glink, channel, signals);
}
EXPORT_SYMBOL(qcom_glink_set_signals);

int qcom_glink_register_signals_cb(struct rpmsg_endpoint *ept,
				   int (*cb)(struct rpmsg_device *, void *, u32, u32))
{
	struct glink_channel *channel;

	if (!ept || !cb)
		return -EINVAL;

	channel = to_glink_channel(ept);
	channel->signals_cb = cb;

	return 0;
}
EXPORT_SYMBOL(qcom_glink_register_signals_cb);

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
	.announce_create = qcom_glink_announce_create,
};

static const struct rpmsg_endpoint_ops glink_endpoint_ops = {
	.destroy_ept = qcom_glink_destroy_ept,
	.send = qcom_glink_send,
	.sendto = qcom_glink_sendto,
	.trysend = qcom_glink_trysend,
	.trysendto = qcom_glink_trysendto,
};

static void qcom_glink_rpdev_release(struct device *dev)
{
	struct rpmsg_device *rpdev = to_rpmsg_device(dev);

	kfree(rpdev->driver_override);
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

	complete_all(&channel->open_req);

	if (create_device) {
		rpdev = kzalloc(sizeof(*rpdev), GFP_KERNEL);
		if (!rpdev) {
			ret = -ENOMEM;
			goto rcid_remove;
		}

		rpdev->ept = &channel->ept;
		strscpy_pad(rpdev->id.name, name, RPMSG_NAME_SIZE);
		rpdev->src = RPMSG_ADDR_ANY;
		rpdev->dst = RPMSG_ADDR_ANY;
		rpdev->ops = &glink_device_ops;

		node = qcom_glink_match_channel(glink->dev->of_node, name);
		rpdev->dev.of_node = node;
		rpdev->dev.parent = glink->dev;
		rpdev->dev.release = qcom_glink_rpdev_release;

		ret = rpmsg_register_device(rpdev);
		if (ret)
			goto rcid_remove;

		channel->rpdev = rpdev;
	}
	CH_INFO(channel, "\n");

	return 0;

rcid_remove:
	CH_INFO(channel, "rcid_remove\n");
	spin_lock_irqsave(&glink->idr_lock, flags);
	idr_remove(&glink->rcids, channel->rcid);
	channel->rcid = 0;
	spin_unlock_irqrestore(&glink->idr_lock, flags);
free_channel:
	CH_INFO(channel, "free_channel\n");
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
	CH_INFO(channel, "\n");

	/* cancel pending rx_done work */
	kthread_cancel_work_sync(&channel->intent_work);

	if (channel->rpdev) {
		strscpy_pad(chinfo.name, channel->name, sizeof(chinfo.name));
		chinfo.src = RPMSG_ADDR_ANY;
		chinfo.dst = RPMSG_ADDR_ANY;

		rpmsg_unregister_device(glink->dev, &chinfo);
	}
	channel->rpdev = NULL;

	qcom_glink_send_close_ack(glink, channel->rcid);

	spin_lock_irqsave(&glink->idr_lock, flags);
	idr_remove(&glink->rcids, channel->rcid);
	channel->rcid = 0;
	spin_unlock_irqrestore(&glink->idr_lock, flags);

	kref_put(&channel->refcount, qcom_glink_channel_release);
}

static void qcom_glink_rx_close_ack(struct qcom_glink *glink, unsigned int lcid)
{
	struct rpmsg_channel_info chinfo;
	struct glink_channel *channel;
	unsigned long flags;

	/* To wakeup any blocking writers */
	wake_up_all(&glink->tx_avail_notify);

	spin_lock_irqsave(&glink->idr_lock, flags);
	channel = idr_find(&glink->lcids, lcid);
	if (WARN(!channel, "close ack on unknown channel\n")) {
		spin_unlock_irqrestore(&glink->idr_lock, flags);
		return;
	}
	CH_INFO(channel, "\n");

	idr_remove(&glink->lcids, channel->lcid);
	channel->lcid = 0;
	spin_unlock_irqrestore(&glink->idr_lock, flags);

	/* Decouple the potential rpdev from the channel */
	if (channel->rpdev) {
		strscpy(chinfo.name, channel->name, sizeof(chinfo.name));
		chinfo.src = RPMSG_ADDR_ANY;
		chinfo.dst = RPMSG_ADDR_ANY;

		rpmsg_unregister_device(glink->dev, &chinfo);
	}
	channel->rpdev = NULL;

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
		case RPM_CMD_RX_INTENT_REQ:
			qcom_glink_handle_intent_req(glink, param1, param2);
			break;
		default:
			WARN(1, "Unknown defer object %d\n", cmd);
			break;
		}

		kfree(dcmd);
	}
}

void qcom_glink_early_ssr_notify(void *data)
{
	struct qcom_glink *glink = data;
	struct glink_channel *channel;
	unsigned long flags;
	int cid;

	if (!glink)
		return;
	atomic_inc(&glink->in_reset);

	/* To wakeup any blocking writers */
	wake_up_all(&glink->tx_avail_notify);

	spin_lock_irqsave(&glink->idr_lock, flags);
	idr_for_each_entry(&glink->lcids, channel, cid) {
		wake_up(&channel->intent_req_ack);
		wake_up(&channel->intent_req_comp);
	}
	spin_unlock_irqrestore(&glink->idr_lock, flags);
}
EXPORT_SYMBOL(qcom_glink_early_ssr_notify);

static void qcom_glink_cancel_rx_work(struct qcom_glink *glink)
{
	struct glink_defer_cmd *dcmd;
	struct glink_defer_cmd *tmp;

	/* cancel any pending deferred rx_work */
	cancel_work_sync(&glink->rx_work);

	list_for_each_entry_safe(dcmd, tmp, &glink->rx_queue, node)
		kfree(dcmd);
}

static ssize_t rpmsg_name_show(struct device *dev,
			       struct device_attribute *attr, char *buf)
{
	int ret = 0;
	const char *name;

	ret = of_property_read_string(dev->of_node, "label", &name);
	if (ret < 0)
		name = dev->of_node->name;

	return sysfs_emit(buf, "%s\n", name);
}
static DEVICE_ATTR_RO(rpmsg_name);

static struct attribute *qcom_glink_attrs[] = {
	&dev_attr_rpmsg_name.attr,
	NULL
};
ATTRIBUTE_GROUPS(qcom_glink);

static void qcom_glink_device_release(struct device *dev)
{
	struct rpmsg_device *rpdev = to_rpmsg_device(dev);
	struct glink_channel *channel = to_glink_channel(rpdev->ept);

	/* Release qcom_glink_alloc_channel() reference */
	kref_put(&channel->refcount, qcom_glink_channel_release);
	kfree(rpdev->driver_override);
	kfree(rpdev);
}

static int qcom_glink_create_chrdev(struct qcom_glink *glink)
{
	struct rpmsg_device *rpdev;
	struct glink_channel *channel;

	rpdev = kzalloc(sizeof(*rpdev), GFP_KERNEL);
	if (!rpdev)
		return -ENOMEM;

	channel = qcom_glink_alloc_channel(glink, "rpmsg_chrdev");
	if (IS_ERR(channel)) {
		kfree(rpdev);
		return PTR_ERR(channel);
	}
	channel->rpdev = rpdev;

	rpdev->ept = &channel->ept;
	rpdev->ops = &glink_device_ops;
	rpdev->dev.parent = glink->dev;
	rpdev->dev.release = qcom_glink_device_release;

	return rpmsg_ctrldev_register_device(rpdev);
}

static void qcom_glink_set_affinity(struct qcom_glink *glink, u32 *arr,
				    size_t size)
{
	int i;

	cpumask_clear(&glink->cpu_mask);
	for (i = 0; i < size; i++) {
		if (arr[i] < num_possible_cpus())
			cpumask_set_cpu(arr[i], &glink->cpu_mask);
	}
	if (irq_set_affinity_hint(glink->irq, &glink->cpu_mask))
		dev_err(glink->dev, "failed to set irq affinity\n");
	if (set_cpus_allowed_ptr(glink->task, &glink->cpu_mask))
		dev_err(glink->dev, "failed to set task affinity\n");
}

struct qcom_glink *qcom_glink_native_probe(struct device *dev,
					   unsigned long features,
					   struct qcom_glink_pipe *rx,
					   struct qcom_glink_pipe *tx,
					   bool intentless)
{
	int ret;
	struct qcom_glink *glink;

	glink = devm_kzalloc(dev, sizeof(*glink), GFP_KERNEL);
	if (!glink)
		return ERR_PTR(-ENOMEM);

	glink->dev = dev;
	glink->tx_pipe = tx;
	glink->rx_pipe = rx;

	glink->features = features;
	glink->intentless = intentless;

	spin_lock_init(&glink->tx_lock);
	spin_lock_init(&glink->rx_lock);
	INIT_LIST_HEAD(&glink->rx_queue);
	INIT_WORK(&glink->rx_work, qcom_glink_work);
	init_waitqueue_head(&glink->tx_avail_notify);

	spin_lock_init(&glink->idr_lock);
	idr_init(&glink->lcids);
	idr_init(&glink->rcids);
	atomic_set(&glink->in_reset, 0);

	glink->dev->groups = qcom_glink_groups;

	ret = device_add_groups(dev, qcom_glink_groups);
	if (ret)
		dev_err(dev, "failed to add groups\n");

	ret = of_property_read_string(dev->of_node, "label", &glink->name);
	if (ret < 0)
		glink->name = dev->of_node->name;

	glink->mbox_client.dev = dev;
	glink->mbox_client.knows_txdone = true;
	glink->mbox_chan = mbox_request_channel(&glink->mbox_client, 0);
	if (IS_ERR(glink->mbox_chan)) {
		if (PTR_ERR(glink->mbox_chan) != -EPROBE_DEFER)
			dev_err(dev, "failed to acquire IPC channel\n");
		return ERR_CAST(glink->mbox_chan);
	}

	kthread_init_worker(&glink->kworker);
	glink->task = kthread_run(kthread_worker_fn, &glink->kworker,
				  "glink_%s", glink->name);
	if (IS_ERR(glink->task)) {
		dev_err(dev, "failed to spawn intent kthread %ld\n",
			PTR_ERR(glink->task));
		return ERR_CAST(glink->task);
	}

	scnprintf(glink->irqname, 32, "glink-native-%s", glink->name);

	glink->ilc = ipc_log_context_create(GLINK_LOG_PAGE_CNT, glink->name, 0);

	return glink;
}
EXPORT_SYMBOL(qcom_glink_native_probe);

int qcom_glink_native_start(struct qcom_glink *glink)
{
	struct device *dev = glink->dev;
	u32 *arr;
	int size;
	int irq;
	int ret;

	spin_lock_init(&glink->irq_lock);
	glink->irq_running = false;

	irq = of_irq_get(dev->of_node, 0);
	ret = devm_request_threaded_irq(dev, irq,
					qcom_glink_native_intr,
					qcom_glink_native_thread_intr,
					IRQF_NO_SUSPEND | IRQF_ONESHOT,
					glink->irqname, glink);
	if (ret) {
		dev_err(dev, "failed to request IRQ with %d\n", ret);
		return ret;
	}

	glink->irq = irq;

	size = of_property_count_u32_elems(dev->of_node, "cpu-affinity");
	if (size > 0) {
		arr = kmalloc_array(size, sizeof(u32), GFP_KERNEL);
		if (!arr)
			return -ENOMEM;
		ret = of_property_read_u32_array(dev->of_node, "cpu-affinity",
						 arr, size);
		if (!ret)
			qcom_glink_set_affinity(glink, arr, size);
		kfree(arr);
	}

	ret = qcom_glink_send_version(glink);
	if (ret) {
		dev_err(glink->dev, "failed to send version: %d\n", ret);
		return ret;
	}

	ret = qcom_glink_create_chrdev(glink);
	if (ret)
		dev_err(glink->dev, "failed to register chrdev\n");

	return 0;
}
EXPORT_SYMBOL(qcom_glink_native_start);

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

	qcom_glink_early_ssr_notify(glink);
	disable_irq(glink->irq);
	qcom_glink_cancel_rx_work(glink);

	ret = device_for_each_child(glink->dev, NULL, qcom_glink_remove_device);
	if (ret)
		dev_warn(glink->dev, "Can't remove GLINK devices: %d\n", ret);

	/* Release any defunct local channels, waiting for close-ack */
	idr_for_each_entry(&glink->lcids, channel, cid) {
		kref_put(&channel->refcount, qcom_glink_channel_release);
		idr_remove(&glink->lcids, cid);
	}

	/* Release any defunct local channels, waiting for close-req */
	idr_for_each_entry(&glink->rcids, channel, cid) {
		kref_put(&channel->refcount, qcom_glink_channel_release);
		idr_remove(&glink->rcids, cid);
	}

	/* Release any defunct local channels, waiting for close-req */
	idr_for_each_entry(&glink->rcids, channel, cid)
		kref_put(&channel->refcount, qcom_glink_channel_release);

	idr_destroy(&glink->lcids);
	idr_destroy(&glink->rcids);

	kthread_flush_worker(&glink->kworker);
	kthread_stop(glink->task);

	/*
	 * Required for spss only. A cb is provided for this in spss driver. For
	 * others, its done in prepare stage in smem driver. No cb is given.
	 */
	qcom_glink_pipe_reset(glink);

	mbox_free_channel(glink->mbox_chan);
}
EXPORT_SYMBOL_GPL(qcom_glink_native_remove);

void qcom_glink_native_unregister(struct qcom_glink *glink)
{
	device_unregister(glink->dev);
}
EXPORT_SYMBOL_GPL(qcom_glink_native_unregister);

static int qcom_glink_suspend_no_irq(struct device *dev)
{
	should_wake = true;

	return 0;
}

static int qcom_glink_resume_no_irq(struct device *dev)
{
	should_wake = false;

	return 0;
}

const struct dev_pm_ops glink_native_pm_ops = {
	.suspend_noirq = qcom_glink_suspend_no_irq,
	.resume_noirq = qcom_glink_resume_no_irq,
};
EXPORT_SYMBOL(glink_native_pm_ops);

MODULE_DESCRIPTION("Qualcomm GLINK driver");
MODULE_LICENSE("GPL v2");
