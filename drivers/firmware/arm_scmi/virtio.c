// SPDX-License-Identifier: GPL-2.0
/*
 * Virtio Transport driver for Arm System Control and Management Interface
 * (SCMI).
 *
 * Copyright (C) 2020-2022 OpenSynergy.
 * Copyright (C) 2021-2022 ARM Ltd.
 */

/**
 * DOC: Theory of Operation
 *
 * The scmi-virtio transport implements a driver for the virtio SCMI device.
 *
 * There is one Tx channel (virtio cmdq, A2P channel) and at most one Rx
 * channel (virtio eventq, P2A channel). Each channel is implemented through a
 * virtqueue. Access to each virtqueue is protected by spinlocks.
 */

#include <linux/completion.h>
#include <linux/errno.h>
#include <linux/refcount.h>
#include <linux/slab.h>
#include <linux/virtio.h>
#include <linux/virtio_config.h>

#include <uapi/linux/virtio_ids.h>
#include <uapi/linux/virtio_scmi.h>

#include "common.h"

#define VIRTIO_MAX_RX_TIMEOUT_MS	60000
#define VIRTIO_SCMI_MAX_MSG_SIZE 128 /* Value may be increased. */
#define VIRTIO_SCMI_MAX_PDU_SIZE \
	(VIRTIO_SCMI_MAX_MSG_SIZE + SCMI_MSG_MAX_PROT_OVERHEAD)
#define DESCRIPTORS_PER_TX_MSG 2

/**
 * struct scmi_vio_channel - Transport channel information
 *
 * @vqueue: Associated virtqueue
 * @cinfo: SCMI Tx or Rx channel
 * @free_lock: Protects access to the @free_list.
 * @free_list: List of unused scmi_vio_msg, maintained for Tx channels only
 * @deferred_tx_work: Worker for TX deferred replies processing
 * @deferred_tx_wq: Workqueue for TX deferred replies
 * @pending_lock: Protects access to the @pending_cmds_list.
 * @pending_cmds_list: List of pre-fetched commands queueud for later processing
 * @is_rx: Whether channel is an Rx channel
 * @max_msg: Maximum number of pending messages for this channel.
 * @lock: Protects access to all members except users, free_list and
 *	  pending_cmds_list.
 * @shutdown_done: A reference to a completion used when freeing this channel.
 * @users: A reference count to currently active users of this channel.
 */
struct scmi_vio_channel {
	struct virtqueue *vqueue;
	struct scmi_chan_info *cinfo;
	/* lock to protect access to the free list. */
	spinlock_t free_lock;
	struct list_head free_list;
	/* lock to protect access to the pending list. */
	spinlock_t pending_lock;
	struct list_head pending_cmds_list;
	struct work_struct deferred_tx_work;
	struct workqueue_struct *deferred_tx_wq;
	bool is_rx;
	unsigned int max_msg;
	/*
	 * Lock to protect access to all members except users, free_list and
	 * pending_cmds_list
	 */
	spinlock_t lock;
	struct completion *shutdown_done;
	refcount_t users;
};

enum poll_states {
	VIO_MSG_NOT_POLLED,
	VIO_MSG_POLL_TIMEOUT,
	VIO_MSG_POLLING,
	VIO_MSG_POLL_DONE,
};

/**
 * struct scmi_vio_msg - Transport PDU information
 *
 * @request: SDU used for commands
 * @input: SDU used for (delayed) responses and notifications
 * @list: List which scmi_vio_msg may be part of
 * @rx_len: Input SDU size in bytes, once input has been received
 * @poll_idx: Last used index registered for polling purposes if this message
 *	      transaction reply was configured for polling.
 * @poll_status: Polling state for this message.
 * @poll_lock: A lock to protect @poll_status
 * @users: A reference count to track this message users and avoid premature
 *	   freeing (and reuse) when polling and IRQ execution paths interleave.
 */
struct scmi_vio_msg {
	struct scmi_msg_payld *request;
	struct scmi_msg_payld *input;
	struct list_head list;
	unsigned int rx_len;
	unsigned int poll_idx;
	enum poll_states poll_status;
	/* Lock to protect access to poll_status */
	spinlock_t poll_lock;
	refcount_t users;
};

/* Only one SCMI VirtIO device can possibly exist */
static struct virtio_device *scmi_vdev;

static void scmi_vio_channel_ready(struct scmi_vio_channel *vioch,
				   struct scmi_chan_info *cinfo)
{
	unsigned long flags;

	spin_lock_irqsave(&vioch->lock, flags);
	cinfo->transport_info = vioch;
	/* Indirectly setting channel not available any more */
	vioch->cinfo = cinfo;
	spin_unlock_irqrestore(&vioch->lock, flags);

	refcount_set(&vioch->users, 1);
}

static inline bool scmi_vio_channel_acquire(struct scmi_vio_channel *vioch)
{
	return refcount_inc_not_zero(&vioch->users);
}

static inline void scmi_vio_channel_release(struct scmi_vio_channel *vioch)
{
	if (refcount_dec_and_test(&vioch->users)) {
		unsigned long flags;

		spin_lock_irqsave(&vioch->lock, flags);
		if (vioch->shutdown_done) {
			vioch->cinfo = NULL;
			complete(vioch->shutdown_done);
		}
		spin_unlock_irqrestore(&vioch->lock, flags);
	}
}

static void scmi_vio_channel_cleanup_sync(struct scmi_vio_channel *vioch)
{
	unsigned long flags;
	DECLARE_COMPLETION_ONSTACK(vioch_shutdown_done);

	/*
	 * Prepare to wait for the last release if not already released
	 * or in progress.
	 */
	spin_lock_irqsave(&vioch->lock, flags);
	if (!vioch->cinfo || vioch->shutdown_done) {
		spin_unlock_irqrestore(&vioch->lock, flags);
		return;
	}

	vioch->shutdown_done = &vioch_shutdown_done;
	virtio_break_device(vioch->vqueue->vdev);
	if (!vioch->is_rx && vioch->deferred_tx_wq)
		/* Cannot be kicked anymore after this...*/
		vioch->deferred_tx_wq = NULL;
	spin_unlock_irqrestore(&vioch->lock, flags);

	scmi_vio_channel_release(vioch);

	/* Let any possibly concurrent RX path release the channel */
	wait_for_completion(vioch->shutdown_done);
}

/* Assumes to be called with vio channel acquired already */
static struct scmi_vio_msg *
scmi_virtio_get_free_msg(struct scmi_vio_channel *vioch)
{
	unsigned long flags;
	struct scmi_vio_msg *msg;

	spin_lock_irqsave(&vioch->free_lock, flags);
	if (list_empty(&vioch->free_list)) {
		spin_unlock_irqrestore(&vioch->free_lock, flags);
		return NULL;
	}

	msg = list_first_entry(&vioch->free_list, typeof(*msg), list);
	list_del_init(&msg->list);
	spin_unlock_irqrestore(&vioch->free_lock, flags);

	/* Still no users, no need to acquire poll_lock */
	msg->poll_status = VIO_MSG_NOT_POLLED;
	refcount_set(&msg->users, 1);

	return msg;
}

static inline bool scmi_vio_msg_acquire(struct scmi_vio_msg *msg)
{
	return refcount_inc_not_zero(&msg->users);
}

/* Assumes to be called with vio channel acquired already */
static inline bool scmi_vio_msg_release(struct scmi_vio_channel *vioch,
					struct scmi_vio_msg *msg)
{
	bool ret;

	ret = refcount_dec_and_test(&msg->users);
	if (ret) {
		unsigned long flags;

		spin_lock_irqsave(&vioch->free_lock, flags);
		list_add_tail(&msg->list, &vioch->free_list);
		spin_unlock_irqrestore(&vioch->free_lock, flags);
	}

	return ret;
}

static bool scmi_vio_have_vq_rx(struct virtio_device *vdev)
{
	return virtio_has_feature(vdev, VIRTIO_SCMI_F_P2A_CHANNELS);
}

static int scmi_vio_feed_vq_rx(struct scmi_vio_channel *vioch,
			       struct scmi_vio_msg *msg)
{
	struct scatterlist sg_in;
	int rc;
	unsigned long flags;
	struct device *dev = &vioch->vqueue->vdev->dev;

	sg_init_one(&sg_in, msg->input, VIRTIO_SCMI_MAX_PDU_SIZE);

	spin_lock_irqsave(&vioch->lock, flags);

	rc = virtqueue_add_inbuf(vioch->vqueue, &sg_in, 1, msg, GFP_ATOMIC);
	if (rc)
		dev_err(dev, "failed to add to RX virtqueue (%d)\n", rc);
	else
		virtqueue_kick(vioch->vqueue);

	spin_unlock_irqrestore(&vioch->lock, flags);

	return rc;
}

/*
 * Assume to be called with channel already acquired or not ready at all;
 * vioch->lock MUST NOT have been already acquired.
 */
static void scmi_finalize_message(struct scmi_vio_channel *vioch,
				  struct scmi_vio_msg *msg)
{
	if (vioch->is_rx)
		scmi_vio_feed_vq_rx(vioch, msg);
	else
		scmi_vio_msg_release(vioch, msg);
}

static void scmi_vio_complete_cb(struct virtqueue *vqueue)
{
	unsigned long flags;
	unsigned int length;
	struct scmi_vio_channel *vioch;
	struct scmi_vio_msg *msg;
	bool cb_enabled = true;

	if (WARN_ON_ONCE(!vqueue->vdev->priv))
		return;
	vioch = &((struct scmi_vio_channel *)vqueue->vdev->priv)[vqueue->index];

	for (;;) {
		if (!scmi_vio_channel_acquire(vioch))
			return;

		spin_lock_irqsave(&vioch->lock, flags);
		if (cb_enabled) {
			virtqueue_disable_cb(vqueue);
			cb_enabled = false;
		}

		msg = virtqueue_get_buf(vqueue, &length);
		if (!msg) {
			if (virtqueue_enable_cb(vqueue)) {
				spin_unlock_irqrestore(&vioch->lock, flags);
				scmi_vio_channel_release(vioch);
				return;
			}
			cb_enabled = true;
		}
		spin_unlock_irqrestore(&vioch->lock, flags);

		if (msg) {
			msg->rx_len = length;
			scmi_rx_callback(vioch->cinfo,
					 msg_read_header(msg->input), msg);

			scmi_finalize_message(vioch, msg);
		}

		/*
		 * Release vio channel between loop iterations to allow
		 * virtio_chan_free() to eventually fully release it when
		 * shutting down; in such a case, any outstanding message will
		 * be ignored since this loop will bail out at the next
		 * iteration.
		 */
		scmi_vio_channel_release(vioch);
	}
}

static void scmi_vio_deferred_tx_worker(struct work_struct *work)
{
	unsigned long flags;
	struct scmi_vio_channel *vioch;
	struct scmi_vio_msg *msg, *tmp;

	vioch = container_of(work, struct scmi_vio_channel, deferred_tx_work);

	if (!scmi_vio_channel_acquire(vioch))
		return;

	/*
	 * Process pre-fetched messages: these could be non-polled messages or
	 * late timed-out replies to polled messages dequeued by chance while
	 * polling for some other messages: this worker is in charge to process
	 * the valid non-expired messages and anyway finally free all of them.
	 */
	spin_lock_irqsave(&vioch->pending_lock, flags);

	/* Scan the list of possibly pre-fetched messages during polling. */
	list_for_each_entry_safe(msg, tmp, &vioch->pending_cmds_list, list) {
		list_del(&msg->list);

		/*
		 * Channel is acquired here (cannot vanish) and this message
		 * is no more processed elsewhere so no poll_lock needed.
		 */
		if (msg->poll_status == VIO_MSG_NOT_POLLED)
			scmi_rx_callback(vioch->cinfo,
					 msg_read_header(msg->input), msg);

		/* Free the processed message once done */
		scmi_vio_msg_release(vioch, msg);
	}

	spin_unlock_irqrestore(&vioch->pending_lock, flags);

	/* Process possibly still pending messages */
	scmi_vio_complete_cb(vioch->vqueue);

	scmi_vio_channel_release(vioch);
}

static const char *const scmi_vio_vqueue_names[] = { "tx", "rx" };

static vq_callback_t *scmi_vio_complete_callbacks[] = {
	scmi_vio_complete_cb,
	scmi_vio_complete_cb
};

static unsigned int virtio_get_max_msg(struct scmi_chan_info *base_cinfo)
{
	struct scmi_vio_channel *vioch = base_cinfo->transport_info;

	return vioch->max_msg;
}

static int virtio_link_supplier(struct device *dev)
{
	if (!scmi_vdev) {
		dev_notice(dev,
			   "Deferring probe after not finding a bound scmi-virtio device\n");
		return -EPROBE_DEFER;
	}

	if (!device_link_add(dev, &scmi_vdev->dev,
			     DL_FLAG_AUTOREMOVE_CONSUMER)) {
		dev_err(dev, "Adding link to supplier virtio device failed\n");
		return -ECANCELED;
	}

	return 0;
}

static bool virtio_chan_available(struct device *dev, int idx)
{
	struct scmi_vio_channel *channels, *vioch = NULL;

	if (WARN_ON_ONCE(!scmi_vdev))
		return false;

	channels = (struct scmi_vio_channel *)scmi_vdev->priv;

	switch (idx) {
	case VIRTIO_SCMI_VQ_TX:
		vioch = &channels[VIRTIO_SCMI_VQ_TX];
		break;
	case VIRTIO_SCMI_VQ_RX:
		if (scmi_vio_have_vq_rx(scmi_vdev))
			vioch = &channels[VIRTIO_SCMI_VQ_RX];
		break;
	default:
		return false;
	}

	return vioch && !vioch->cinfo;
}

static void scmi_destroy_tx_workqueue(void *deferred_tx_wq)
{
	destroy_workqueue(deferred_tx_wq);
}

static int virtio_chan_setup(struct scmi_chan_info *cinfo, struct device *dev,
			     bool tx)
{
	struct scmi_vio_channel *vioch;
	int index = tx ? VIRTIO_SCMI_VQ_TX : VIRTIO_SCMI_VQ_RX;
	int i;

	if (!scmi_vdev)
		return -EPROBE_DEFER;

	vioch = &((struct scmi_vio_channel *)scmi_vdev->priv)[index];

	/* Setup a deferred worker for polling. */
	if (tx && !vioch->deferred_tx_wq) {
		int ret;

		vioch->deferred_tx_wq =
			alloc_workqueue(dev_name(&scmi_vdev->dev),
					WQ_UNBOUND | WQ_FREEZABLE | WQ_SYSFS,
					0);
		if (!vioch->deferred_tx_wq)
			return -ENOMEM;

		ret = devm_add_action_or_reset(dev, scmi_destroy_tx_workqueue,
					       vioch->deferred_tx_wq);
		if (ret)
			return ret;

		INIT_WORK(&vioch->deferred_tx_work,
			  scmi_vio_deferred_tx_worker);
	}

	for (i = 0; i < vioch->max_msg; i++) {
		struct scmi_vio_msg *msg;

		msg = devm_kzalloc(dev, sizeof(*msg), GFP_KERNEL);
		if (!msg)
			return -ENOMEM;

		if (tx) {
			msg->request = devm_kzalloc(dev,
						    VIRTIO_SCMI_MAX_PDU_SIZE,
						    GFP_KERNEL);
			if (!msg->request)
				return -ENOMEM;
			spin_lock_init(&msg->poll_lock);
			refcount_set(&msg->users, 1);
		}

		msg->input = devm_kzalloc(dev, VIRTIO_SCMI_MAX_PDU_SIZE,
					  GFP_KERNEL);
		if (!msg->input)
			return -ENOMEM;

		scmi_finalize_message(vioch, msg);
	}

	scmi_vio_channel_ready(vioch, cinfo);

	return 0;
}

static int virtio_chan_free(int id, void *p, void *data)
{
	struct scmi_chan_info *cinfo = p;
	struct scmi_vio_channel *vioch = cinfo->transport_info;

	scmi_vio_channel_cleanup_sync(vioch);

	scmi_free_channel(cinfo, data, id);

	return 0;
}

static int virtio_send_message(struct scmi_chan_info *cinfo,
			       struct scmi_xfer *xfer)
{
	struct scmi_vio_channel *vioch = cinfo->transport_info;
	struct scatterlist sg_out;
	struct scatterlist sg_in;
	struct scatterlist *sgs[DESCRIPTORS_PER_TX_MSG] = { &sg_out, &sg_in };
	unsigned long flags;
	int rc;
	struct scmi_vio_msg *msg;

	if (!scmi_vio_channel_acquire(vioch))
		return -EINVAL;

	msg = scmi_virtio_get_free_msg(vioch);
	if (!msg) {
		scmi_vio_channel_release(vioch);
		return -EBUSY;
	}

	msg_tx_prepare(msg->request, xfer);

	sg_init_one(&sg_out, msg->request, msg_command_size(xfer));
	sg_init_one(&sg_in, msg->input, msg_response_size(xfer));

	spin_lock_irqsave(&vioch->lock, flags);

	/*
	 * If polling was requested for this transaction:
	 *  - retrieve last used index (will be used as polling reference)
	 *  - bind the polled message to the xfer via .priv
	 *  - grab an additional msg refcount for the poll-path
	 */
	if (xfer->hdr.poll_completion) {
		msg->poll_idx = virtqueue_enable_cb_prepare(vioch->vqueue);
		/* Still no users, no need to acquire poll_lock */
		msg->poll_status = VIO_MSG_POLLING;
		scmi_vio_msg_acquire(msg);
		/* Ensure initialized msg is visibly bound to xfer */
		smp_store_mb(xfer->priv, msg);
	}

	rc = virtqueue_add_sgs(vioch->vqueue, sgs, 1, 1, msg, GFP_ATOMIC);
	if (rc)
		dev_err(vioch->cinfo->dev,
			"failed to add to TX virtqueue (%d)\n", rc);
	else
		virtqueue_kick(vioch->vqueue);

	spin_unlock_irqrestore(&vioch->lock, flags);

	if (rc) {
		/* Ensure order between xfer->priv clear and vq feeding */
		smp_store_mb(xfer->priv, NULL);
		if (xfer->hdr.poll_completion)
			scmi_vio_msg_release(vioch, msg);
		scmi_vio_msg_release(vioch, msg);
	}

	scmi_vio_channel_release(vioch);

	return rc;
}

static void virtio_fetch_response(struct scmi_chan_info *cinfo,
				  struct scmi_xfer *xfer)
{
	struct scmi_vio_msg *msg = xfer->priv;

	if (msg)
		msg_fetch_response(msg->input, msg->rx_len, xfer);
}

static void virtio_fetch_notification(struct scmi_chan_info *cinfo,
				      size_t max_len, struct scmi_xfer *xfer)
{
	struct scmi_vio_msg *msg = xfer->priv;

	if (msg)
		msg_fetch_notification(msg->input, msg->rx_len, max_len, xfer);
}

/**
 * virtio_mark_txdone  - Mark transmission done
 *
 * Free only completed polling transfer messages.
 *
 * Note that in the SCMI VirtIO transport we never explicitly release still
 * outstanding but timed-out messages by forcibly re-adding them to the
 * free-list inside the TX code path; we instead let IRQ/RX callbacks, or the
 * TX deferred worker, eventually clean up such messages once, finally, a late
 * reply is received and discarded (if ever).
 *
 * This approach was deemed preferable since those pending timed-out buffers are
 * still effectively owned by the SCMI platform VirtIO device even after timeout
 * expiration: forcibly freeing and reusing them before they had been returned
 * explicitly by the SCMI platform could lead to subtle bugs due to message
 * corruption.
 * An SCMI platform VirtIO device which never returns message buffers is
 * anyway broken and it will quickly lead to exhaustion of available messages.
 *
 * For this same reason, here, we take care to free only the polled messages
 * that had been somehow replied (only if not by chance already processed on the
 * IRQ path - the initial scmi_vio_msg_release() takes care of this) and also
 * any timed-out polled message if that indeed appears to have been at least
 * dequeued from the virtqueues (VIO_MSG_POLL_DONE): this is needed since such
 * messages won't be freed elsewhere. Any other polled message is marked as
 * VIO_MSG_POLL_TIMEOUT.
 *
 * Possible late replies to timed-out polled messages will be eventually freed
 * by RX callbacks if delivered on the IRQ path or by the deferred TX worker if
 * dequeued on some other polling path.
 *
 * @cinfo: SCMI channel info
 * @ret: Transmission return code
 * @xfer: Transfer descriptor
 */
static void virtio_mark_txdone(struct scmi_chan_info *cinfo, int ret,
			       struct scmi_xfer *xfer)
{
	unsigned long flags;
	struct scmi_vio_channel *vioch = cinfo->transport_info;
	struct scmi_vio_msg *msg = xfer->priv;

	if (!msg || !scmi_vio_channel_acquire(vioch))
		return;

	/* Ensure msg is unbound from xfer anyway at this point */
	smp_store_mb(xfer->priv, NULL);

	/* Must be a polled xfer and not already freed on the IRQ path */
	if (!xfer->hdr.poll_completion || scmi_vio_msg_release(vioch, msg)) {
		scmi_vio_channel_release(vioch);
		return;
	}

	spin_lock_irqsave(&msg->poll_lock, flags);
	/* Do not free timedout polled messages only if still inflight */
	if (ret != -ETIMEDOUT || msg->poll_status == VIO_MSG_POLL_DONE)
		scmi_vio_msg_release(vioch, msg);
	else if (msg->poll_status == VIO_MSG_POLLING)
		msg->poll_status = VIO_MSG_POLL_TIMEOUT;
	spin_unlock_irqrestore(&msg->poll_lock, flags);

	scmi_vio_channel_release(vioch);
}

/**
 * virtio_poll_done  - Provide polling support for VirtIO transport
 *
 * @cinfo: SCMI channel info
 * @xfer: Reference to the transfer being poll for.
 *
 * VirtIO core provides a polling mechanism based only on last used indexes:
 * this means that it is possible to poll the virtqueues waiting for something
 * new to arrive from the host side, but the only way to check if the freshly
 * arrived buffer was indeed what we were waiting for is to compare the newly
 * arrived message descriptor with the one we are polling on.
 *
 * As a consequence it can happen to dequeue something different from the buffer
 * we were poll-waiting for: if that is the case such early fetched buffers are
 * then added to a the @pending_cmds_list list for later processing by a
 * dedicated deferred worker.
 *
 * So, basically, once something new is spotted we proceed to de-queue all the
 * freshly received used buffers until we found the one we were polling on, or,
 * we have 'seemingly' emptied the virtqueue; if some buffers are still pending
 * in the vqueue at the end of the polling loop (possible due to inherent races
 * in virtqueues handling mechanisms), we similarly kick the deferred worker
 * and let it process those, to avoid indefinitely looping in the .poll_done
 * busy-waiting helper.
 *
 * Finally, we delegate to the deferred worker also the final free of any timed
 * out reply to a polled message that we should dequeue.
 *
 * Note that, since we do NOT have per-message suppress notification mechanism,
 * the message we are polling for could be alternatively delivered via usual
 * IRQs callbacks on another core which happened to have IRQs enabled while we
 * are actively polling for it here: in such a case it will be handled as such
 * by scmi_rx_callback() and the polling loop in the SCMI Core TX path will be
 * transparently terminated anyway.
 *
 * Return: True once polling has successfully completed.
 */
static bool virtio_poll_done(struct scmi_chan_info *cinfo,
			     struct scmi_xfer *xfer)
{
	bool pending, found = false;
	unsigned int length, any_prefetched = 0;
	unsigned long flags;
	struct scmi_vio_msg *next_msg, *msg = xfer->priv;
	struct scmi_vio_channel *vioch = cinfo->transport_info;

	if (!msg)
		return true;

	/*
	 * Processed already by other polling loop on another CPU ?
	 *
	 * Note that this message is acquired on the poll path so cannot vanish
	 * while inside this loop iteration even if concurrently processed on
	 * the IRQ path.
	 *
	 * Avoid to acquire poll_lock since polled_status can be changed
	 * in a relevant manner only later in this same thread of execution:
	 * any other possible changes made concurrently by other polling loops
	 * or by a reply delivered on the IRQ path have no meaningful impact on
	 * this loop iteration: in other words it is harmless to allow this
	 * possible race but let has avoid spinlocking with irqs off in this
	 * initial part of the polling loop.
	 */
	if (msg->poll_status == VIO_MSG_POLL_DONE)
		return true;

	if (!scmi_vio_channel_acquire(vioch))
		return true;

	/* Has cmdq index moved at all ? */
	pending = virtqueue_poll(vioch->vqueue, msg->poll_idx);
	if (!pending) {
		scmi_vio_channel_release(vioch);
		return false;
	}

	spin_lock_irqsave(&vioch->lock, flags);
	virtqueue_disable_cb(vioch->vqueue);

	/*
	 * Process all new messages till the polled-for message is found OR
	 * the vqueue is empty.
	 */
	while ((next_msg = virtqueue_get_buf(vioch->vqueue, &length))) {
		bool next_msg_done = false;

		/*
		 * Mark any dequeued buffer message as VIO_MSG_POLL_DONE so
		 * that can be properly freed even on timeout in mark_txdone.
		 */
		spin_lock(&next_msg->poll_lock);
		if (next_msg->poll_status == VIO_MSG_POLLING) {
			next_msg->poll_status = VIO_MSG_POLL_DONE;
			next_msg_done = true;
		}
		spin_unlock(&next_msg->poll_lock);

		next_msg->rx_len = length;
		/* Is the message we were polling for ? */
		if (next_msg == msg) {
			found = true;
			break;
		} else if (next_msg_done) {
			/* Skip the rest if this was another polled msg */
			continue;
		}

		/*
		 * Enqueue for later processing any non-polled message and any
		 * timed-out polled one that we happen to have dequeued.
		 */
		spin_lock(&next_msg->poll_lock);
		if (next_msg->poll_status == VIO_MSG_NOT_POLLED ||
		    next_msg->poll_status == VIO_MSG_POLL_TIMEOUT) {
			spin_unlock(&next_msg->poll_lock);

			any_prefetched++;
			spin_lock(&vioch->pending_lock);
			list_add_tail(&next_msg->list,
				      &vioch->pending_cmds_list);
			spin_unlock(&vioch->pending_lock);
		} else {
			spin_unlock(&next_msg->poll_lock);
		}
	}

	/*
	 * When the polling loop has successfully terminated if something
	 * else was queued in the meantime, it will be served by a deferred
	 * worker OR by the normal IRQ/callback OR by other poll loops.
	 *
	 * If we are still looking for the polled reply, the polling index has
	 * to be updated to the current vqueue last used index.
	 */
	if (found) {
		pending = !virtqueue_enable_cb(vioch->vqueue);
	} else {
		msg->poll_idx = virtqueue_enable_cb_prepare(vioch->vqueue);
		pending = virtqueue_poll(vioch->vqueue, msg->poll_idx);
	}

	if (vioch->deferred_tx_wq && (any_prefetched || pending))
		queue_work(vioch->deferred_tx_wq, &vioch->deferred_tx_work);

	spin_unlock_irqrestore(&vioch->lock, flags);

	scmi_vio_channel_release(vioch);

	return found;
}

static const struct scmi_transport_ops scmi_virtio_ops = {
	.link_supplier = virtio_link_supplier,
	.chan_available = virtio_chan_available,
	.chan_setup = virtio_chan_setup,
	.chan_free = virtio_chan_free,
	.get_max_msg = virtio_get_max_msg,
	.send_message = virtio_send_message,
	.fetch_response = virtio_fetch_response,
	.fetch_notification = virtio_fetch_notification,
	.mark_txdone = virtio_mark_txdone,
	.poll_done = virtio_poll_done,
};

static int scmi_vio_probe(struct virtio_device *vdev)
{
	struct device *dev = &vdev->dev;
	struct scmi_vio_channel *channels;
	bool have_vq_rx;
	int vq_cnt;
	int i;
	int ret;
	struct virtqueue *vqs[VIRTIO_SCMI_VQ_MAX_CNT];

	/* Only one SCMI VirtiO device allowed */
	if (scmi_vdev) {
		dev_err(dev,
			"One SCMI Virtio device was already initialized: only one allowed.\n");
		return -EBUSY;
	}

	have_vq_rx = scmi_vio_have_vq_rx(vdev);
	vq_cnt = have_vq_rx ? VIRTIO_SCMI_VQ_MAX_CNT : 1;

	channels = devm_kcalloc(dev, vq_cnt, sizeof(*channels), GFP_KERNEL);
	if (!channels)
		return -ENOMEM;

	if (have_vq_rx)
		channels[VIRTIO_SCMI_VQ_RX].is_rx = true;

	ret = virtio_find_vqs(vdev, vq_cnt, vqs, scmi_vio_complete_callbacks,
			      scmi_vio_vqueue_names, NULL);
	if (ret) {
		dev_err(dev, "Failed to get %d virtqueue(s)\n", vq_cnt);
		return ret;
	}

	for (i = 0; i < vq_cnt; i++) {
		unsigned int sz;

		spin_lock_init(&channels[i].lock);
		spin_lock_init(&channels[i].free_lock);
		INIT_LIST_HEAD(&channels[i].free_list);
		spin_lock_init(&channels[i].pending_lock);
		INIT_LIST_HEAD(&channels[i].pending_cmds_list);
		channels[i].vqueue = vqs[i];

		sz = virtqueue_get_vring_size(channels[i].vqueue);
		/* Tx messages need multiple descriptors. */
		if (!channels[i].is_rx)
			sz /= DESCRIPTORS_PER_TX_MSG;

		if (sz > MSG_TOKEN_MAX) {
			dev_info(dev,
				 "%s virtqueue could hold %d messages. Only %ld allowed to be pending.\n",
				 channels[i].is_rx ? "rx" : "tx",
				 sz, MSG_TOKEN_MAX);
			sz = MSG_TOKEN_MAX;
		}
		channels[i].max_msg = sz;
	}

	vdev->priv = channels;
	/* Ensure initialized scmi_vdev is visible */
	smp_store_mb(scmi_vdev, vdev);

	return 0;
}

static void scmi_vio_remove(struct virtio_device *vdev)
{
	/*
	 * Once we get here, virtio_chan_free() will have already been called by
	 * the SCMI core for any existing channel and, as a consequence, all the
	 * virtio channels will have been already marked NOT ready, causing any
	 * outstanding message on any vqueue to be ignored by complete_cb: now
	 * we can just stop processing buffers and destroy the vqueues.
	 */
	virtio_reset_device(vdev);
	vdev->config->del_vqs(vdev);
	/* Ensure scmi_vdev is visible as NULL */
	smp_store_mb(scmi_vdev, NULL);
}

static int scmi_vio_validate(struct virtio_device *vdev)
{
#ifdef CONFIG_ARM_SCMI_TRANSPORT_VIRTIO_VERSION1_COMPLIANCE
	if (!virtio_has_feature(vdev, VIRTIO_F_VERSION_1)) {
		dev_err(&vdev->dev,
			"device does not comply with spec version 1.x\n");
		return -EINVAL;
	}
#endif
	return 0;
}

static unsigned int features[] = {
	VIRTIO_SCMI_F_P2A_CHANNELS,
};

static const struct virtio_device_id id_table[] = {
	{ VIRTIO_ID_SCMI, VIRTIO_DEV_ANY_ID },
	{ 0 }
};

static struct virtio_driver virtio_scmi_driver = {
	.driver.name = "scmi-virtio",
	.driver.owner = THIS_MODULE,
	.feature_table = features,
	.feature_table_size = ARRAY_SIZE(features),
	.id_table = id_table,
	.probe = scmi_vio_probe,
	.remove = scmi_vio_remove,
	.validate = scmi_vio_validate,
};

static int __init virtio_scmi_init(void)
{
	return register_virtio_driver(&virtio_scmi_driver);
}

static void virtio_scmi_exit(void)
{
	unregister_virtio_driver(&virtio_scmi_driver);
}

const struct scmi_desc scmi_virtio_desc = {
	.transport_init = virtio_scmi_init,
	.transport_exit = virtio_scmi_exit,
	.ops = &scmi_virtio_ops,
	/* for non-realtime virtio devices */
	.max_rx_timeout_ms = VIRTIO_MAX_RX_TIMEOUT_MS,
	.max_msg = 0, /* overridden by virtio_get_max_msg() */
	.max_msg_size = VIRTIO_SCMI_MAX_MSG_SIZE,
	.atomic_enabled = IS_ENABLED(CONFIG_ARM_SCMI_TRANSPORT_VIRTIO_ATOMIC_ENABLE),
};
