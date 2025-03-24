// SPDX-License-Identifier: GPL-2.0
/*
 * System Control and Management Interface (SCMI) Raw mode support
 *
 * Copyright (C) 2022 ARM Ltd.
 */
/**
 * DOC: Theory of operation
 *
 * When enabled the SCMI Raw mode support exposes a userspace API which allows
 * to send and receive SCMI commands, replies and notifications from a user
 * application through injection and snooping of bare SCMI messages in binary
 * little-endian format.
 *
 * Such injected SCMI transactions will then be routed through the SCMI core
 * stack towards the SCMI backend server using whatever SCMI transport is
 * currently configured on the system under test.
 *
 * It is meant to help in running any sort of SCMI backend server testing, no
 * matter where the server is placed, as long as it is normally reachable via
 * the transport configured on the system.
 *
 * It is activated by a Kernel configuration option since it is NOT meant to
 * be used in production but only during development and in CI deployments.
 *
 * In order to avoid possible interferences between the SCMI Raw transactions
 * originated from a test-suite and the normal operations of the SCMI drivers,
 * when Raw mode is enabled, by default, all the regular SCMI drivers are
 * inhibited, unless CONFIG_ARM_SCMI_RAW_MODE_SUPPORT_COEX is enabled: in this
 * latter case the regular SCMI stack drivers will be loaded as usual and it is
 * up to the user of this interface to take care of manually inhibiting the
 * regular SCMI drivers in order to avoid interferences during the test runs.
 *
 * The exposed API is as follows.
 *
 * All SCMI Raw entries are rooted under a common top /raw debugfs top directory
 * which in turn is rooted under the corresponding underlying  SCMI instance.
 *
 * /sys/kernel/debug/scmi/
 * `-- 0
 *     |-- atomic_threshold_us
 *     |-- instance_name
 *     |-- raw
 *     |   |-- channels
 *     |   |   |-- 0x10
 *     |   |   |   |-- message
 *     |   |   |   `-- message_async
 *     |   |   `-- 0x13
 *     |   |       |-- message
 *     |   |       `-- message_async
 *     |   |-- errors
 *     |   |-- message
 *     |   |-- message_async
 *     |   |-- notification
 *     |   `-- reset
 *     `-- transport
 *         |-- is_atomic
 *         |-- max_msg_size
 *         |-- max_rx_timeout_ms
 *         |-- rx_max_msg
 *         |-- tx_max_msg
 *         `-- type
 *
 * where:
 *
 *  - errors: used to read back timed-out and unexpected replies
 *  - message*: used to send sync/async commands and read back immediate and
 *		delayed reponses (if any)
 *  - notification: used to read any notification being emitted by the system
 *		    (if previously enabled by the user app)
 *  - reset: used to flush the queues of messages (of any kind) still pending
 *	     to be read; this is useful at test-suite start/stop to get
 *	     rid of any unread messages from the previous run.
 *
 * with the per-channel entries rooted at /channels being present only on a
 * system where multiple transport channels have been configured.
 *
 * Such per-channel entries can be used to explicitly choose a specific channel
 * for SCMI bare message injection, in contrast with the general entries above
 * where, instead, the selection of the proper channel to use is automatically
 * performed based the protocol embedded in the injected message and on how the
 * transport is configured on the system.
 *
 * Note that other common general entries are available under transport/ to let
 * the user applications properly make up their expectations in terms of
 * timeouts and message characteristics.
 *
 * Each write to the message* entries causes one command request to be built
 * and sent while the replies or delayed response are read back from those same
 * entries one message at time (receiving an EOF at each message boundary).
 *
 * The user application running the test is in charge of handling timeouts
 * on replies and properly choosing SCMI sequence numbers for the outgoing
 * requests (using the same sequence number is supported but discouraged).
 *
 * Injection of multiple in-flight requests is supported as long as the user
 * application uses properly distinct sequence numbers for concurrent requests
 * and takes care to properly manage all the related issues about concurrency
 * and command/reply pairing. Keep in mind that, anyway, the real level of
 * parallelism attainable in such scenario is dependent on the characteristics
 * of the underlying transport being used.
 *
 * Since the SCMI core regular stack is partially used to deliver and collect
 * the messages, late replies arrived after timeouts and any other sort of
 * unexpected message can be identified by the SCMI core as usual and they will
 * be reported as messages under "errors" for later analysis.
 */

#include <linux/bitmap.h>
#include <linux/debugfs.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/export.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/poll.h>
#include <linux/of.h>
#include <linux/slab.h>
#include <linux/xarray.h>

#include "common.h"

#include "raw_mode.h"

#include <trace/events/scmi.h>

#define SCMI_XFER_RAW_MAX_RETRIES	10

/**
 * struct scmi_raw_queue  - Generic Raw queue descriptor
 *
 * @free_bufs: A freelists listhead used to keep unused raw buffers
 * @free_bufs_lock: Spinlock used to protect access to @free_bufs
 * @msg_q: A listhead to a queue of snooped messages waiting to be read out
 * @msg_q_lock: Spinlock used to protect access to @msg_q
 * @wq: A waitqueue used to wait and poll on related @msg_q
 */
struct scmi_raw_queue {
	struct list_head free_bufs;
	/* Protect free_bufs[] lists */
	spinlock_t free_bufs_lock;
	struct list_head msg_q;
	/* Protect msg_q[] lists */
	spinlock_t msg_q_lock;
	wait_queue_head_t wq;
};

/**
 * struct scmi_raw_mode_info  - Structure holding SCMI Raw instance data
 *
 * @id: Sequential Raw instance ID.
 * @handle: Pointer to SCMI entity handle to use
 * @desc: Pointer to the transport descriptor to use
 * @tx_max_msg: Maximum number of concurrent TX in-flight messages
 * @q: An array of Raw queue descriptors
 * @chans_q: An XArray mapping optional additional per-channel queues
 * @free_waiters: Head of freelist for unused waiters
 * @free_mtx: A mutex to protect the waiters freelist
 * @active_waiters: Head of list for currently active and used waiters
 * @active_mtx: A mutex to protect the active waiters list
 * @waiters_work: A work descriptor to be used with the workqueue machinery
 * @wait_wq: A workqueue reference to the created workqueue
 * @dentry: Top debugfs root dentry for SCMI Raw
 * @gid: A group ID used for devres accounting
 *
 * Note that this descriptor is passed back to the core after SCMI Raw is
 * initialized as an opaque handle to use by subsequent SCMI Raw call hooks.
 *
 */
struct scmi_raw_mode_info {
	unsigned int id;
	const struct scmi_handle *handle;
	const struct scmi_desc *desc;
	int tx_max_msg;
	struct scmi_raw_queue *q[SCMI_RAW_MAX_QUEUE];
	struct xarray chans_q;
	struct list_head free_waiters;
	/* Protect free_waiters list */
	struct mutex free_mtx;
	struct list_head active_waiters;
	/* Protect active_waiters list */
	struct mutex active_mtx;
	struct work_struct waiters_work;
	struct workqueue_struct	*wait_wq;
	struct dentry *dentry;
	void *gid;
};

/**
 * struct scmi_xfer_raw_waiter  - Structure to describe an xfer to be waited for
 *
 * @start_jiffies: The timestamp in jiffies of when this structure was queued.
 * @cinfo: A reference to the channel to use for this transaction
 * @xfer: A reference to the xfer to be waited for
 * @async_response: A completion to be, optionally, used for async waits: it
 *		    will be setup by @scmi_do_xfer_raw_start, if needed, to be
 *		    pointed at by xfer->async_done.
 * @node: A list node.
 */
struct scmi_xfer_raw_waiter {
	unsigned long start_jiffies;
	struct scmi_chan_info *cinfo;
	struct scmi_xfer *xfer;
	struct completion async_response;
	struct list_head node;
};

/**
 * struct scmi_raw_buffer  - Structure to hold a full SCMI message
 *
 * @max_len: The maximum allowed message size (header included) that can be
 *	     stored into @msg
 * @msg: A message buffer used to collect a full message grabbed from an xfer.
 * @node: A list node.
 */
struct scmi_raw_buffer {
	size_t max_len;
	struct scmi_msg msg;
	struct list_head node;
};

/**
 * struct scmi_dbg_raw_data  - Structure holding data needed by the debugfs
 * layer
 *
 * @chan_id: The preferred channel to use: if zero the channel is automatically
 *	     selected based on protocol.
 * @raw: A reference to the Raw instance.
 * @tx: A message buffer used to collect TX message on write.
 * @tx_size: The effective size of the TX message.
 * @tx_req_size: The final expected size of the complete TX message.
 * @rx: A message buffer to collect RX message on read.
 * @rx_size: The effective size of the RX message.
 */
struct scmi_dbg_raw_data {
	u8 chan_id;
	struct scmi_raw_mode_info *raw;
	struct scmi_msg tx;
	size_t tx_size;
	size_t tx_req_size;
	struct scmi_msg rx;
	size_t rx_size;
};

static struct scmi_raw_queue *
scmi_raw_queue_select(struct scmi_raw_mode_info *raw, unsigned int idx,
		      unsigned int chan_id)
{
	if (!chan_id)
		return raw->q[idx];

	return xa_load(&raw->chans_q, chan_id);
}

static struct scmi_raw_buffer *scmi_raw_buffer_get(struct scmi_raw_queue *q)
{
	unsigned long flags;
	struct scmi_raw_buffer *rb = NULL;
	struct list_head *head = &q->free_bufs;

	spin_lock_irqsave(&q->free_bufs_lock, flags);
	if (!list_empty(head)) {
		rb = list_first_entry(head, struct scmi_raw_buffer, node);
		list_del_init(&rb->node);
	}
	spin_unlock_irqrestore(&q->free_bufs_lock, flags);

	return rb;
}

static void scmi_raw_buffer_put(struct scmi_raw_queue *q,
				struct scmi_raw_buffer *rb)
{
	unsigned long flags;

	/* Reset to full buffer length */
	rb->msg.len = rb->max_len;

	spin_lock_irqsave(&q->free_bufs_lock, flags);
	list_add_tail(&rb->node, &q->free_bufs);
	spin_unlock_irqrestore(&q->free_bufs_lock, flags);
}

static void scmi_raw_buffer_enqueue(struct scmi_raw_queue *q,
				    struct scmi_raw_buffer *rb)
{
	unsigned long flags;

	spin_lock_irqsave(&q->msg_q_lock, flags);
	list_add_tail(&rb->node, &q->msg_q);
	spin_unlock_irqrestore(&q->msg_q_lock, flags);

	wake_up_interruptible(&q->wq);
}

static struct scmi_raw_buffer*
scmi_raw_buffer_dequeue_unlocked(struct scmi_raw_queue *q)
{
	struct scmi_raw_buffer *rb = NULL;

	if (!list_empty(&q->msg_q)) {
		rb = list_first_entry(&q->msg_q, struct scmi_raw_buffer, node);
		list_del_init(&rb->node);
	}

	return rb;
}

static struct scmi_raw_buffer *scmi_raw_buffer_dequeue(struct scmi_raw_queue *q)
{
	unsigned long flags;
	struct scmi_raw_buffer *rb;

	spin_lock_irqsave(&q->msg_q_lock, flags);
	rb = scmi_raw_buffer_dequeue_unlocked(q);
	spin_unlock_irqrestore(&q->msg_q_lock, flags);

	return rb;
}

static void scmi_raw_buffer_queue_flush(struct scmi_raw_queue *q)
{
	struct scmi_raw_buffer *rb;

	do {
		rb = scmi_raw_buffer_dequeue(q);
		if (rb)
			scmi_raw_buffer_put(q, rb);
	} while (rb);
}

static struct scmi_xfer_raw_waiter *
scmi_xfer_raw_waiter_get(struct scmi_raw_mode_info *raw, struct scmi_xfer *xfer,
			 struct scmi_chan_info *cinfo, bool async)
{
	struct scmi_xfer_raw_waiter *rw = NULL;

	mutex_lock(&raw->free_mtx);
	if (!list_empty(&raw->free_waiters)) {
		rw = list_first_entry(&raw->free_waiters,
				      struct scmi_xfer_raw_waiter, node);
		list_del_init(&rw->node);

		if (async) {
			reinit_completion(&rw->async_response);
			xfer->async_done = &rw->async_response;
		}

		rw->cinfo = cinfo;
		rw->xfer = xfer;
	}
	mutex_unlock(&raw->free_mtx);

	return rw;
}

static void scmi_xfer_raw_waiter_put(struct scmi_raw_mode_info *raw,
				     struct scmi_xfer_raw_waiter *rw)
{
	if (rw->xfer) {
		rw->xfer->async_done = NULL;
		rw->xfer = NULL;
	}

	mutex_lock(&raw->free_mtx);
	list_add_tail(&rw->node, &raw->free_waiters);
	mutex_unlock(&raw->free_mtx);
}

static void scmi_xfer_raw_waiter_enqueue(struct scmi_raw_mode_info *raw,
					 struct scmi_xfer_raw_waiter *rw)
{
	/* A timestamp for the deferred worker to know how much this has aged */
	rw->start_jiffies = jiffies;

	trace_scmi_xfer_response_wait(rw->xfer->transfer_id, rw->xfer->hdr.id,
				      rw->xfer->hdr.protocol_id,
				      rw->xfer->hdr.seq,
				      raw->desc->max_rx_timeout_ms,
				      rw->xfer->hdr.poll_completion);

	mutex_lock(&raw->active_mtx);
	list_add_tail(&rw->node, &raw->active_waiters);
	mutex_unlock(&raw->active_mtx);

	/* kick waiter work */
	queue_work(raw->wait_wq, &raw->waiters_work);
}

static struct scmi_xfer_raw_waiter *
scmi_xfer_raw_waiter_dequeue(struct scmi_raw_mode_info *raw)
{
	struct scmi_xfer_raw_waiter *rw = NULL;

	mutex_lock(&raw->active_mtx);
	if (!list_empty(&raw->active_waiters)) {
		rw = list_first_entry(&raw->active_waiters,
				      struct scmi_xfer_raw_waiter, node);
		list_del_init(&rw->node);
	}
	mutex_unlock(&raw->active_mtx);

	return rw;
}

/**
 * scmi_xfer_raw_worker  - Work function to wait for Raw xfers completions
 *
 * @work: A reference to the work.
 *
 * In SCMI Raw mode, once a user-provided injected SCMI message is sent, we
 * cannot wait to receive its response (if any) in the context of the injection
 * routines so as not to leave the userspace write syscall, which delivered the
 * SCMI message to send, pending till eventually a reply is received.
 * Userspace should and will poll/wait instead on the read syscalls which will
 * be in charge of reading a received reply (if any).
 *
 * Even though reply messages are collected and reported into the SCMI Raw layer
 * on the RX path, nonetheless we have to properly wait for their completion as
 * usual (and async_completion too if needed) in order to properly release the
 * xfer structure at the end: to do this out of the context of the write/send
 * these waiting jobs are delegated to this deferred worker.
 *
 * Any sent xfer, to be waited for, is timestamped and queued for later
 * consumption by this worker: queue aging is accounted for while choosing a
 * timeout for the completion, BUT we do not really care here if we end up
 * accidentally waiting for a bit too long.
 */
static void scmi_xfer_raw_worker(struct work_struct *work)
{
	struct scmi_raw_mode_info *raw;
	struct device *dev;
	unsigned long max_tmo;

	raw = container_of(work, struct scmi_raw_mode_info, waiters_work);
	dev = raw->handle->dev;
	max_tmo = msecs_to_jiffies(raw->desc->max_rx_timeout_ms);

	do {
		int ret = 0;
		unsigned int timeout_ms;
		unsigned long aging;
		struct scmi_xfer *xfer;
		struct scmi_xfer_raw_waiter *rw;
		struct scmi_chan_info *cinfo;

		rw = scmi_xfer_raw_waiter_dequeue(raw);
		if (!rw)
			return;

		cinfo = rw->cinfo;
		xfer = rw->xfer;
		/*
		 * Waiters are queued by wait-deadline at the end, so some of
		 * them could have been already expired when processed, BUT we
		 * have to check the completion status anyway just in case a
		 * virtually expired (aged) transaction was indeed completed
		 * fine and we'll have to wait for the asynchronous part (if
		 * any): for this reason a 1 ms timeout is used for already
		 * expired/aged xfers.
		 */
		aging = jiffies - rw->start_jiffies;
		timeout_ms = max_tmo > aging ?
			jiffies_to_msecs(max_tmo - aging) : 1;

		ret = scmi_xfer_raw_wait_for_message_response(cinfo, xfer,
							      timeout_ms);
		if (!ret && xfer->hdr.status)
			ret = scmi_to_linux_errno(xfer->hdr.status);

		if (raw->desc->ops->mark_txdone)
			raw->desc->ops->mark_txdone(rw->cinfo, ret, xfer);

		trace_scmi_xfer_end(xfer->transfer_id, xfer->hdr.id,
				    xfer->hdr.protocol_id, xfer->hdr.seq, ret);

		/* Wait also for an async delayed response if needed */
		if (!ret && xfer->async_done) {
			unsigned long tmo = msecs_to_jiffies(SCMI_MAX_RESPONSE_TIMEOUT);

			if (!wait_for_completion_timeout(xfer->async_done, tmo))
				dev_err(dev,
					"timed out in RAW delayed resp - HDR:%08X\n",
					pack_scmi_header(&xfer->hdr));
		}

		/* Release waiter and xfer */
		scmi_xfer_raw_put(raw->handle, xfer);
		scmi_xfer_raw_waiter_put(raw, rw);
	} while (1);
}

static void scmi_xfer_raw_reset(struct scmi_raw_mode_info *raw)
{
	int i;

	dev_info(raw->handle->dev, "Resetting SCMI Raw stack.\n");

	for (i = 0; i < SCMI_RAW_MAX_QUEUE; i++)
		scmi_raw_buffer_queue_flush(raw->q[i]);
}

/**
 * scmi_xfer_raw_get_init  - An helper to build a valid xfer from the provided
 * bare SCMI message.
 *
 * @raw: A reference to the Raw instance.
 * @buf: A buffer containing the whole SCMI message to send (including the
 *	 header) in little-endian binary formmat.
 * @len: Length of the message in @buf.
 * @p: A pointer to return the initialized Raw xfer.
 *
 * After an xfer is picked from the TX pool and filled in with the message
 * content, the xfer is registered as pending with the core in the usual way
 * using the original sequence number provided by the user with the message.
 *
 * Note that, in case the testing user application is NOT using distinct
 * sequence-numbers between successive SCMI messages such registration could
 * fail temporarily if the previous message, using the same sequence number,
 * had still not released; in such a case we just wait and retry.
 *
 * Return: 0 on Success
 */
static int scmi_xfer_raw_get_init(struct scmi_raw_mode_info *raw, void *buf,
				  size_t len, struct scmi_xfer **p)
{
	u32 msg_hdr;
	size_t tx_size;
	struct scmi_xfer *xfer;
	int ret, retry = SCMI_XFER_RAW_MAX_RETRIES;
	struct device *dev = raw->handle->dev;

	if (!buf || len < sizeof(u32))
		return -EINVAL;

	tx_size = len - sizeof(u32);
	/* Ensure we have sane transfer sizes */
	if (tx_size > raw->desc->max_msg_size)
		return -ERANGE;

	xfer = scmi_xfer_raw_get(raw->handle);
	if (IS_ERR(xfer)) {
		dev_warn(dev, "RAW - Cannot get a free RAW xfer !\n");
		return PTR_ERR(xfer);
	}

	/* Build xfer from the provided SCMI bare LE message */
	msg_hdr = le32_to_cpu(*((__le32 *)buf));
	unpack_scmi_header(msg_hdr, &xfer->hdr);
	xfer->hdr.seq = (u16)MSG_XTRACT_TOKEN(msg_hdr);
	/* Polling not supported */
	xfer->hdr.poll_completion = false;
	xfer->hdr.status = SCMI_SUCCESS;
	xfer->tx.len = tx_size;
	xfer->rx.len = raw->desc->max_msg_size;
	/* Clear the whole TX buffer */
	memset(xfer->tx.buf, 0x00, raw->desc->max_msg_size);
	if (xfer->tx.len)
		memcpy(xfer->tx.buf, (u8 *)buf + sizeof(msg_hdr), xfer->tx.len);
	*p = xfer;

	/*
	 * In flight registration can temporarily fail in case of Raw messages
	 * if the user injects messages without using monotonically increasing
	 * sequence numbers since, in Raw mode, the xfer (and the token) is
	 * finally released later by a deferred worker. Just retry for a while.
	 */
	do {
		ret = scmi_xfer_raw_inflight_register(raw->handle, xfer);
		if (ret) {
			dev_dbg(dev,
				"...retrying[%d] inflight registration\n",
				retry);
			msleep(raw->desc->max_rx_timeout_ms /
			       SCMI_XFER_RAW_MAX_RETRIES);
		}
	} while (ret && --retry);

	if (ret) {
		dev_warn(dev,
			 "RAW - Could NOT register xfer %d in-flight HDR:0x%08X\n",
			 xfer->hdr.seq, msg_hdr);
		scmi_xfer_raw_put(raw->handle, xfer);
	}

	return ret;
}

/**
 * scmi_do_xfer_raw_start  - An helper to send a valid raw xfer
 *
 * @raw: A reference to the Raw instance.
 * @xfer: The xfer to send
 * @chan_id: The channel ID to use, if zero the channels is automatically
 *	     selected based on the protocol used.
 * @async: A flag stating if an asynchronous command is required.
 *
 * This function send a previously built raw xfer using an appropriate channel
 * and queues the related waiting work.
 *
 * Note that we need to know explicitly if the required command is meant to be
 * asynchronous in kind since we have to properly setup the waiter.
 * (and deducing this from the payload is weak and do not scale given there is
 *  NOT a common header-flag stating if the command is asynchronous or not)
 *
 * Return: 0 on Success
 */
static int scmi_do_xfer_raw_start(struct scmi_raw_mode_info *raw,
				  struct scmi_xfer *xfer, u8 chan_id,
				  bool async)
{
	int ret;
	struct scmi_chan_info *cinfo;
	struct scmi_xfer_raw_waiter *rw;
	struct device *dev = raw->handle->dev;

	if (!chan_id)
		chan_id = xfer->hdr.protocol_id;
	else
		xfer->flags |= SCMI_XFER_FLAG_CHAN_SET;

	cinfo = scmi_xfer_raw_channel_get(raw->handle, chan_id);
	if (IS_ERR(cinfo))
		return PTR_ERR(cinfo);

	rw = scmi_xfer_raw_waiter_get(raw, xfer, cinfo, async);
	if (!rw) {
		dev_warn(dev, "RAW - Cannot get a free waiter !\n");
		return -ENOMEM;
	}

	/* True ONLY if also supported by transport. */
	if (is_polling_enabled(cinfo, raw->desc))
		xfer->hdr.poll_completion = true;

	reinit_completion(&xfer->done);
	/* Make sure xfer state update is visible before sending */
	smp_store_mb(xfer->state, SCMI_XFER_SENT_OK);

	trace_scmi_xfer_begin(xfer->transfer_id, xfer->hdr.id,
			      xfer->hdr.protocol_id, xfer->hdr.seq,
			      xfer->hdr.poll_completion);

	ret = raw->desc->ops->send_message(rw->cinfo, xfer);
	if (ret) {
		dev_err(dev, "Failed to send RAW message %d\n", ret);
		scmi_xfer_raw_waiter_put(raw, rw);
		return ret;
	}

	trace_scmi_msg_dump(raw->id, cinfo->id, xfer->hdr.protocol_id,
			    xfer->hdr.id, "cmnd", xfer->hdr.seq,
			    xfer->hdr.status,
			    xfer->tx.buf, xfer->tx.len);

	scmi_xfer_raw_waiter_enqueue(raw, rw);

	return ret;
}

/**
 * scmi_raw_message_send  - An helper to build and send an SCMI command using
 * the provided SCMI bare message buffer
 *
 * @raw: A reference to the Raw instance.
 * @buf: A buffer containing the whole SCMI message to send (including the
 *	 header) in little-endian binary format.
 * @len: Length of the message in @buf.
 * @chan_id: The channel ID to use.
 * @async: A flag stating if an asynchronous command is required.
 *
 * Return: 0 on Success
 */
static int scmi_raw_message_send(struct scmi_raw_mode_info *raw,
				 void *buf, size_t len, u8 chan_id, bool async)
{
	int ret;
	struct scmi_xfer *xfer;

	ret = scmi_xfer_raw_get_init(raw, buf, len, &xfer);
	if (ret)
		return ret;

	ret = scmi_do_xfer_raw_start(raw, xfer, chan_id, async);
	if (ret)
		scmi_xfer_raw_put(raw->handle, xfer);

	return ret;
}

static struct scmi_raw_buffer *
scmi_raw_message_dequeue(struct scmi_raw_queue *q, bool o_nonblock)
{
	unsigned long flags;
	struct scmi_raw_buffer *rb;

	spin_lock_irqsave(&q->msg_q_lock, flags);
	while (list_empty(&q->msg_q)) {
		spin_unlock_irqrestore(&q->msg_q_lock, flags);

		if (o_nonblock)
			return ERR_PTR(-EAGAIN);

		if (wait_event_interruptible(q->wq, !list_empty(&q->msg_q)))
			return ERR_PTR(-ERESTARTSYS);

		spin_lock_irqsave(&q->msg_q_lock, flags);
	}

	rb = scmi_raw_buffer_dequeue_unlocked(q);

	spin_unlock_irqrestore(&q->msg_q_lock, flags);

	return rb;
}

/**
 * scmi_raw_message_receive  - An helper to dequeue and report the next
 * available enqueued raw message payload that has been collected.
 *
 * @raw: A reference to the Raw instance.
 * @buf: A buffer to get hold of the whole SCMI message received and represented
 *	 in little-endian binary format.
 * @len: Length of @buf.
 * @size: The effective size of the message copied into @buf
 * @idx: The index of the queue to pick the next queued message from.
 * @chan_id: The channel ID to use.
 * @o_nonblock: A flag to request a non-blocking message dequeue.
 *
 * Return: 0 on Success
 */
static int scmi_raw_message_receive(struct scmi_raw_mode_info *raw,
				    void *buf, size_t len, size_t *size,
				    unsigned int idx, unsigned int chan_id,
				    bool o_nonblock)
{
	int ret = 0;
	struct scmi_raw_buffer *rb;
	struct scmi_raw_queue *q;

	q = scmi_raw_queue_select(raw, idx, chan_id);
	if (!q)
		return -ENODEV;

	rb = scmi_raw_message_dequeue(q, o_nonblock);
	if (IS_ERR(rb)) {
		dev_dbg(raw->handle->dev, "RAW - No message available!\n");
		return PTR_ERR(rb);
	}

	if (rb->msg.len <= len) {
		memcpy(buf, rb->msg.buf, rb->msg.len);
		*size = rb->msg.len;
	} else {
		ret = -ENOSPC;
	}

	scmi_raw_buffer_put(q, rb);

	return ret;
}

/* SCMI Raw debugfs helpers */

static ssize_t scmi_dbg_raw_mode_common_read(struct file *filp,
					     char __user *buf,
					     size_t count, loff_t *ppos,
					     unsigned int idx)
{
	ssize_t cnt;
	struct scmi_dbg_raw_data *rd = filp->private_data;

	if (!rd->rx_size) {
		int ret;

		ret = scmi_raw_message_receive(rd->raw, rd->rx.buf, rd->rx.len,
					       &rd->rx_size, idx, rd->chan_id,
					       filp->f_flags & O_NONBLOCK);
		if (ret) {
			rd->rx_size = 0;
			return ret;
		}

		/* Reset any previous filepos change, including writes */
		*ppos = 0;
	} else if (*ppos == rd->rx_size) {
		/* Return EOF once all the message has been read-out */
		rd->rx_size = 0;
		return 0;
	}

	cnt = simple_read_from_buffer(buf, count, ppos,
				      rd->rx.buf, rd->rx_size);

	return cnt;
}

static ssize_t scmi_dbg_raw_mode_common_write(struct file *filp,
					      const char __user *buf,
					      size_t count, loff_t *ppos,
					      bool async)
{
	int ret;
	struct scmi_dbg_raw_data *rd = filp->private_data;

	if (count > rd->tx.len - rd->tx_size)
		return -ENOSPC;

	/* On first write attempt @count carries the total full message size. */
	if (!rd->tx_size)
		rd->tx_req_size = count;

	/*
	 * Gather a full message, possibly across multiple interrupted wrrtes,
	 * before sending it with a single RAW xfer.
	 */
	if (rd->tx_size < rd->tx_req_size) {
		ssize_t cnt;

		cnt = simple_write_to_buffer(rd->tx.buf, rd->tx.len, ppos,
					     buf, count);
		if (cnt < 0)
			return cnt;

		rd->tx_size += cnt;
		if (cnt < count)
			return cnt;
	}

	ret = scmi_raw_message_send(rd->raw, rd->tx.buf, rd->tx_size,
				    rd->chan_id, async);

	/* Reset ppos for next message ... */
	rd->tx_size = 0;
	*ppos = 0;

	return ret ?: count;
}

static __poll_t scmi_test_dbg_raw_common_poll(struct file *filp,
					      struct poll_table_struct *wait,
					      unsigned int idx)
{
	unsigned long flags;
	struct scmi_dbg_raw_data *rd = filp->private_data;
	struct scmi_raw_queue *q;
	__poll_t mask = 0;

	q = scmi_raw_queue_select(rd->raw, idx, rd->chan_id);
	if (!q)
		return mask;

	poll_wait(filp, &q->wq, wait);

	spin_lock_irqsave(&q->msg_q_lock, flags);
	if (!list_empty(&q->msg_q))
		mask = EPOLLIN | EPOLLRDNORM;
	spin_unlock_irqrestore(&q->msg_q_lock, flags);

	return mask;
}

static ssize_t scmi_dbg_raw_mode_message_read(struct file *filp,
					      char __user *buf,
					      size_t count, loff_t *ppos)
{
	return scmi_dbg_raw_mode_common_read(filp, buf, count, ppos,
					     SCMI_RAW_REPLY_QUEUE);
}

static ssize_t scmi_dbg_raw_mode_message_write(struct file *filp,
					       const char __user *buf,
					       size_t count, loff_t *ppos)
{
	return scmi_dbg_raw_mode_common_write(filp, buf, count, ppos, false);
}

static __poll_t scmi_dbg_raw_mode_message_poll(struct file *filp,
					       struct poll_table_struct *wait)
{
	return scmi_test_dbg_raw_common_poll(filp, wait, SCMI_RAW_REPLY_QUEUE);
}

static int scmi_dbg_raw_mode_open(struct inode *inode, struct file *filp)
{
	struct scmi_raw_mode_info *raw;
	struct scmi_dbg_raw_data *rd;

	if (!inode->i_private)
		return -ENODEV;

	raw = inode->i_private;
	rd = kzalloc(sizeof(*rd), GFP_KERNEL);
	if (!rd)
		return -ENOMEM;

	rd->rx.len = raw->desc->max_msg_size + sizeof(u32);
	rd->rx.buf = kzalloc(rd->rx.len, GFP_KERNEL);
	if (!rd->rx.buf) {
		kfree(rd);
		return -ENOMEM;
	}

	rd->tx.len = raw->desc->max_msg_size + sizeof(u32);
	rd->tx.buf = kzalloc(rd->tx.len, GFP_KERNEL);
	if (!rd->tx.buf) {
		kfree(rd->rx.buf);
		kfree(rd);
		return -ENOMEM;
	}

	/* Grab channel ID from debugfs entry naming if any */
	/* not set - reassing 0 we already had after kzalloc() */
	rd->chan_id = debugfs_get_aux_num(filp);

	rd->raw = raw;
	filp->private_data = rd;

	return nonseekable_open(inode, filp);
}

static int scmi_dbg_raw_mode_release(struct inode *inode, struct file *filp)
{
	struct scmi_dbg_raw_data *rd = filp->private_data;

	kfree(rd->rx.buf);
	kfree(rd->tx.buf);
	kfree(rd);

	return 0;
}

static ssize_t scmi_dbg_raw_mode_reset_write(struct file *filp,
					     const char __user *buf,
					     size_t count, loff_t *ppos)
{
	struct scmi_dbg_raw_data *rd = filp->private_data;

	scmi_xfer_raw_reset(rd->raw);

	return count;
}

static const struct file_operations scmi_dbg_raw_mode_reset_fops = {
	.open = scmi_dbg_raw_mode_open,
	.release = scmi_dbg_raw_mode_release,
	.write = scmi_dbg_raw_mode_reset_write,
	.owner = THIS_MODULE,
};

static const struct file_operations scmi_dbg_raw_mode_message_fops = {
	.open = scmi_dbg_raw_mode_open,
	.release = scmi_dbg_raw_mode_release,
	.read = scmi_dbg_raw_mode_message_read,
	.write = scmi_dbg_raw_mode_message_write,
	.poll = scmi_dbg_raw_mode_message_poll,
	.owner = THIS_MODULE,
};

static ssize_t scmi_dbg_raw_mode_message_async_write(struct file *filp,
						     const char __user *buf,
						     size_t count, loff_t *ppos)
{
	return scmi_dbg_raw_mode_common_write(filp, buf, count, ppos, true);
}

static const struct file_operations scmi_dbg_raw_mode_message_async_fops = {
	.open = scmi_dbg_raw_mode_open,
	.release = scmi_dbg_raw_mode_release,
	.read = scmi_dbg_raw_mode_message_read,
	.write = scmi_dbg_raw_mode_message_async_write,
	.poll = scmi_dbg_raw_mode_message_poll,
	.owner = THIS_MODULE,
};

static ssize_t scmi_test_dbg_raw_mode_notif_read(struct file *filp,
						 char __user *buf,
						 size_t count, loff_t *ppos)
{
	return scmi_dbg_raw_mode_common_read(filp, buf, count, ppos,
					     SCMI_RAW_NOTIF_QUEUE);
}

static __poll_t
scmi_test_dbg_raw_mode_notif_poll(struct file *filp,
				  struct poll_table_struct *wait)
{
	return scmi_test_dbg_raw_common_poll(filp, wait, SCMI_RAW_NOTIF_QUEUE);
}

static const struct file_operations scmi_dbg_raw_mode_notification_fops = {
	.open = scmi_dbg_raw_mode_open,
	.release = scmi_dbg_raw_mode_release,
	.read = scmi_test_dbg_raw_mode_notif_read,
	.poll = scmi_test_dbg_raw_mode_notif_poll,
	.owner = THIS_MODULE,
};

static ssize_t scmi_test_dbg_raw_mode_errors_read(struct file *filp,
						  char __user *buf,
						  size_t count, loff_t *ppos)
{
	return scmi_dbg_raw_mode_common_read(filp, buf, count, ppos,
					     SCMI_RAW_ERRS_QUEUE);
}

static __poll_t
scmi_test_dbg_raw_mode_errors_poll(struct file *filp,
				   struct poll_table_struct *wait)
{
	return scmi_test_dbg_raw_common_poll(filp, wait, SCMI_RAW_ERRS_QUEUE);
}

static const struct file_operations scmi_dbg_raw_mode_errors_fops = {
	.open = scmi_dbg_raw_mode_open,
	.release = scmi_dbg_raw_mode_release,
	.read = scmi_test_dbg_raw_mode_errors_read,
	.poll = scmi_test_dbg_raw_mode_errors_poll,
	.owner = THIS_MODULE,
};

static struct scmi_raw_queue *
scmi_raw_queue_init(struct scmi_raw_mode_info *raw)
{
	int i;
	struct scmi_raw_buffer *rb;
	struct device *dev = raw->handle->dev;
	struct scmi_raw_queue *q;

	q = devm_kzalloc(dev, sizeof(*q), GFP_KERNEL);
	if (!q)
		return ERR_PTR(-ENOMEM);

	rb = devm_kcalloc(dev, raw->tx_max_msg, sizeof(*rb), GFP_KERNEL);
	if (!rb)
		return ERR_PTR(-ENOMEM);

	spin_lock_init(&q->free_bufs_lock);
	INIT_LIST_HEAD(&q->free_bufs);
	for (i = 0; i < raw->tx_max_msg; i++, rb++) {
		rb->max_len = raw->desc->max_msg_size + sizeof(u32);
		rb->msg.buf = devm_kzalloc(dev, rb->max_len, GFP_KERNEL);
		if (!rb->msg.buf)
			return ERR_PTR(-ENOMEM);
		scmi_raw_buffer_put(q, rb);
	}

	spin_lock_init(&q->msg_q_lock);
	INIT_LIST_HEAD(&q->msg_q);
	init_waitqueue_head(&q->wq);

	return q;
}

static int scmi_xfer_raw_worker_init(struct scmi_raw_mode_info *raw)
{
	int i;
	struct scmi_xfer_raw_waiter *rw;
	struct device *dev = raw->handle->dev;

	rw = devm_kcalloc(dev, raw->tx_max_msg, sizeof(*rw), GFP_KERNEL);
	if (!rw)
		return -ENOMEM;

	raw->wait_wq = alloc_workqueue("scmi-raw-wait-wq-%d",
				       WQ_UNBOUND | WQ_FREEZABLE |
				       WQ_HIGHPRI | WQ_SYSFS, 0, raw->id);
	if (!raw->wait_wq)
		return -ENOMEM;

	mutex_init(&raw->free_mtx);
	INIT_LIST_HEAD(&raw->free_waiters);
	mutex_init(&raw->active_mtx);
	INIT_LIST_HEAD(&raw->active_waiters);

	for (i = 0; i < raw->tx_max_msg; i++, rw++) {
		init_completion(&rw->async_response);
		scmi_xfer_raw_waiter_put(raw, rw);
	}
	INIT_WORK(&raw->waiters_work, scmi_xfer_raw_worker);

	return 0;
}

static int scmi_raw_mode_setup(struct scmi_raw_mode_info *raw,
			       u8 *channels, int num_chans)
{
	int ret, idx;
	void *gid;
	struct device *dev = raw->handle->dev;

	gid = devres_open_group(dev, NULL, GFP_KERNEL);
	if (!gid)
		return -ENOMEM;

	for (idx = 0; idx < SCMI_RAW_MAX_QUEUE; idx++) {
		raw->q[idx] = scmi_raw_queue_init(raw);
		if (IS_ERR(raw->q[idx])) {
			ret = PTR_ERR(raw->q[idx]);
			goto err;
		}
	}

	xa_init(&raw->chans_q);
	if (num_chans > 1) {
		int i;

		for (i = 0; i < num_chans; i++) {
			struct scmi_raw_queue *q;

			q = scmi_raw_queue_init(raw);
			if (IS_ERR(q)) {
				ret = PTR_ERR(q);
				goto err_xa;
			}

			ret = xa_insert(&raw->chans_q, channels[i], q,
					GFP_KERNEL);
			if (ret) {
				dev_err(dev,
					"Fail to allocate Raw queue 0x%02X\n",
					channels[i]);
				goto err_xa;
			}
		}
	}

	ret = scmi_xfer_raw_worker_init(raw);
	if (ret)
		goto err_xa;

	devres_close_group(dev, gid);
	raw->gid = gid;

	return 0;

err_xa:
	xa_destroy(&raw->chans_q);
err:
	devres_release_group(dev, gid);
	return ret;
}

/**
 * scmi_raw_mode_init  - Function to initialize the SCMI Raw stack
 *
 * @handle: Pointer to SCMI entity handle
 * @top_dentry: A reference to the top Raw debugfs dentry
 * @instance_id: The ID of the underlying SCMI platform instance represented by
 *		 this Raw instance
 * @channels: The list of the existing channels
 * @num_chans: The number of entries in @channels
 * @desc: Reference to the transport operations
 * @tx_max_msg: Max number of in-flight messages allowed by the transport
 *
 * This function prepare the SCMI Raw stack and creates the debugfs API.
 *
 * Return: An opaque handle to the Raw instance on Success, an ERR_PTR otherwise
 */
void *scmi_raw_mode_init(const struct scmi_handle *handle,
			 struct dentry *top_dentry, int instance_id,
			 u8 *channels, int num_chans,
			 const struct scmi_desc *desc, int tx_max_msg)
{
	int ret;
	struct scmi_raw_mode_info *raw;
	struct device *dev;

	if (!handle || !desc)
		return ERR_PTR(-EINVAL);

	dev = handle->dev;
	raw = devm_kzalloc(dev, sizeof(*raw), GFP_KERNEL);
	if (!raw)
		return ERR_PTR(-ENOMEM);

	raw->handle = handle;
	raw->desc = desc;
	raw->tx_max_msg = tx_max_msg;
	raw->id = instance_id;

	ret = scmi_raw_mode_setup(raw, channels, num_chans);
	if (ret) {
		devm_kfree(dev, raw);
		return ERR_PTR(ret);
	}

	raw->dentry = debugfs_create_dir("raw", top_dentry);

	debugfs_create_file("reset", 0200, raw->dentry, raw,
			    &scmi_dbg_raw_mode_reset_fops);

	debugfs_create_file("message", 0600, raw->dentry, raw,
			    &scmi_dbg_raw_mode_message_fops);

	debugfs_create_file("message_async", 0600, raw->dentry, raw,
			    &scmi_dbg_raw_mode_message_async_fops);

	debugfs_create_file("notification", 0400, raw->dentry, raw,
			    &scmi_dbg_raw_mode_notification_fops);

	debugfs_create_file("errors", 0400, raw->dentry, raw,
			    &scmi_dbg_raw_mode_errors_fops);

	/*
	 * Expose per-channel entries if multiple channels available.
	 * Just ignore errors while setting up these interfaces since we
	 * have anyway already a working core Raw support.
	 */
	if (num_chans > 1) {
		int i;
		struct dentry *top_chans;

		top_chans = debugfs_create_dir("channels", raw->dentry);

		for (i = 0; i < num_chans; i++) {
			char cdir[8];
			struct dentry *chd;

			snprintf(cdir, 8, "0x%02X", channels[i]);
			chd = debugfs_create_dir(cdir, top_chans);

			debugfs_create_file_aux_num("message", 0600, chd,
					    raw, channels[i],
					    &scmi_dbg_raw_mode_message_fops);

			debugfs_create_file_aux_num("message_async", 0600, chd,
					    raw, channels[i],
					    &scmi_dbg_raw_mode_message_async_fops);
		}
	}

	dev_info(dev, "SCMI RAW Mode initialized for instance %d\n", raw->id);

	return raw;
}

/**
 * scmi_raw_mode_cleanup  - Function to cleanup the SCMI Raw stack
 *
 * @r: An opaque handle to an initialized SCMI Raw instance
 */
void scmi_raw_mode_cleanup(void *r)
{
	struct scmi_raw_mode_info *raw = r;

	if (!raw)
		return;

	debugfs_remove_recursive(raw->dentry);

	cancel_work_sync(&raw->waiters_work);
	destroy_workqueue(raw->wait_wq);
	xa_destroy(&raw->chans_q);
}

static int scmi_xfer_raw_collect(void *msg, size_t *msg_len,
				 struct scmi_xfer *xfer)
{
	__le32 *m;
	size_t msg_size;

	if (!xfer || !msg || !msg_len)
		return -EINVAL;

	/* Account for hdr ...*/
	msg_size = xfer->rx.len + sizeof(u32);
	/* ... and status if needed */
	if (xfer->hdr.type != MSG_TYPE_NOTIFICATION)
		msg_size += sizeof(u32);

	if (msg_size > *msg_len)
		return -ENOSPC;

	m = msg;
	*m = cpu_to_le32(pack_scmi_header(&xfer->hdr));
	if (xfer->hdr.type != MSG_TYPE_NOTIFICATION)
		*++m = cpu_to_le32(xfer->hdr.status);

	memcpy(++m, xfer->rx.buf, xfer->rx.len);

	*msg_len = msg_size;

	return 0;
}

/**
 * scmi_raw_message_report  - Helper to report back valid reponses/notifications
 * to raw message requests.
 *
 * @r: An opaque reference to the raw instance configuration
 * @xfer: The xfer containing the message to be reported
 * @idx: The index of the queue.
 * @chan_id: The channel ID to use.
 *
 * If Raw mode is enabled, this is called from the SCMI core on the regular RX
 * path to save and enqueue the response/notification payload carried by this
 * xfer into a dedicated scmi_raw_buffer for later consumption by the user.
 *
 * This way the caller can free the related xfer immediately afterwards and the
 * user can read back the raw message payload at its own pace (if ever) without
 * holding an xfer for too long.
 */
void scmi_raw_message_report(void *r, struct scmi_xfer *xfer,
			     unsigned int idx, unsigned int chan_id)
{
	int ret;
	unsigned long flags;
	struct scmi_raw_buffer *rb;
	struct device *dev;
	struct scmi_raw_queue *q;
	struct scmi_raw_mode_info *raw = r;

	if (!raw || (idx == SCMI_RAW_REPLY_QUEUE && !SCMI_XFER_IS_RAW(xfer)))
		return;

	dev = raw->handle->dev;
	q = scmi_raw_queue_select(raw, idx,
				  SCMI_XFER_IS_CHAN_SET(xfer) ? chan_id : 0);
	if (!q) {
		dev_warn(dev,
			 "RAW[%d] - NO queue for chan 0x%X. Dropping report.\n",
			 idx, chan_id);
		return;
	}

	/*
	 * Grab the msg_q_lock upfront to avoid a possible race between
	 * realizing the free list was empty and effectively picking the next
	 * buffer to use from the oldest one enqueued and still unread on this
	 * msg_q.
	 *
	 * Note that nowhere else these locks are taken together, so no risk of
	 * deadlocks du eto inversion.
	 */
	spin_lock_irqsave(&q->msg_q_lock, flags);
	rb = scmi_raw_buffer_get(q);
	if (!rb) {
		/*
		 * Immediate and delayed replies to previously injected Raw
		 * commands MUST be read back from userspace to free the buffers:
		 * if this is not happening something is seriously broken and
		 * must be fixed at the application level: complain loudly.
		 */
		if (idx == SCMI_RAW_REPLY_QUEUE) {
			spin_unlock_irqrestore(&q->msg_q_lock, flags);
			dev_warn(dev,
				 "RAW[%d] - Buffers exhausted. Dropping report.\n",
				 idx);
			return;
		}

		/*
		 * Notifications and errors queues are instead handled in a
		 * circular manner: unread old buffers are just overwritten by
		 * newer ones.
		 *
		 * The main reason for this is that notifications originated
		 * by Raw requests cannot be distinguished from normal ones, so
		 * your Raw buffers queues risk to be flooded and depleted by
		 * notifications if you left it mistakenly enabled or when in
		 * coexistence mode.
		 */
		rb = scmi_raw_buffer_dequeue_unlocked(q);
		if (WARN_ON(!rb)) {
			spin_unlock_irqrestore(&q->msg_q_lock, flags);
			return;
		}

		/* Reset to full buffer length */
		rb->msg.len = rb->max_len;

		dev_warn_once(dev,
			      "RAW[%d] - Buffers exhausted. Re-using oldest.\n",
			      idx);
	}
	spin_unlock_irqrestore(&q->msg_q_lock, flags);

	ret = scmi_xfer_raw_collect(rb->msg.buf, &rb->msg.len, xfer);
	if (ret) {
		dev_warn(dev, "RAW - Cannot collect xfer into buffer !\n");
		scmi_raw_buffer_put(q, rb);
		return;
	}

	scmi_raw_buffer_enqueue(q, rb);
}

static void scmi_xfer_raw_fill(struct scmi_raw_mode_info *raw,
			       struct scmi_chan_info *cinfo,
			       struct scmi_xfer *xfer, u32 msg_hdr)
{
	/* Unpack received HDR as it is */
	unpack_scmi_header(msg_hdr, &xfer->hdr);
	xfer->hdr.seq = MSG_XTRACT_TOKEN(msg_hdr);

	memset(xfer->rx.buf, 0x00, xfer->rx.len);

	raw->desc->ops->fetch_response(cinfo, xfer);
}

/**
 * scmi_raw_error_report  - Helper to report back timed-out or generally
 * unexpected replies.
 *
 * @r: An opaque reference to the raw instance configuration
 * @cinfo: A reference to the channel to use to retrieve the broken xfer
 * @msg_hdr: The SCMI message header of the message to fetch and report
 * @priv: Any private data related to the xfer.
 *
 * If Raw mode is enabled, this is called from the SCMI core on the RX path in
 * case of errors to save and enqueue the bad message payload carried by the
 * message that has just been received.
 *
 * Note that we have to manually fetch any available payload into a temporary
 * xfer to be able to save and enqueue the message, since the regular RX error
 * path which had called this would have not fetched the message payload having
 * classified it as an error.
 */
void scmi_raw_error_report(void *r, struct scmi_chan_info *cinfo,
			   u32 msg_hdr, void *priv)
{
	struct scmi_xfer xfer;
	struct scmi_raw_mode_info *raw = r;

	if (!raw)
		return;

	xfer.rx.len = raw->desc->max_msg_size;
	xfer.rx.buf = kzalloc(xfer.rx.len, GFP_ATOMIC);
	if (!xfer.rx.buf) {
		dev_info(raw->handle->dev,
			 "Cannot report Raw error for HDR:0x%X - ENOMEM\n",
			 msg_hdr);
		return;
	}

	/* Any transport-provided priv must be passed back down to transport */
	if (priv)
		/* Ensure priv is visible */
		smp_store_mb(xfer.priv, priv);

	scmi_xfer_raw_fill(raw, cinfo, &xfer, msg_hdr);
	scmi_raw_message_report(raw, &xfer, SCMI_RAW_ERRS_QUEUE, 0);

	kfree(xfer.rx.buf);
}
