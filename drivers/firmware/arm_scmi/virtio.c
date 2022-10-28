// SPDX-License-Identifier: GPL-2.0
/*
 * Virtio Transport driver for Arm System Control and Management Interface
 * (SCMI).
 *
 * Copyright (C) 2020-2021 OpenSynergy.
 * Copyright (C) 2021 ARM Ltd.
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

#include <linux/errno.h>
#include <linux/slab.h>
#include <linux/virtio.h>
#include <linux/virtio_config.h>

#include <uapi/linux/virtio_ids.h>
#include <uapi/linux/virtio_scmi.h>

#include "common.h"

#define VIRTIO_SCMI_MAX_MSG_SIZE 128 /* Value may be increased. */
#define VIRTIO_SCMI_MAX_PDU_SIZE \
	(VIRTIO_SCMI_MAX_MSG_SIZE + SCMI_MSG_MAX_PROT_OVERHEAD)
#define DESCRIPTORS_PER_TX_MSG 2

/**
 * struct scmi_vio_channel - Transport channel information
 *
 * @vqueue: Associated virtqueue
 * @cinfo: SCMI Tx or Rx channel
 * @free_list: List of unused scmi_vio_msg, maintained for Tx channels only
 * @is_rx: Whether channel is an Rx channel
 * @ready: Whether transport user is ready to hear about channel
 * @max_msg: Maximum number of pending messages for this channel.
 * @lock: Protects access to all members except ready.
 * @ready_lock: Protects access to ready. If required, it must be taken before
 *              lock.
 */
struct scmi_vio_channel {
	struct virtqueue *vqueue;
	struct scmi_chan_info *cinfo;
	struct list_head free_list;
	bool is_rx;
	bool ready;
	unsigned int max_msg;
	/* lock to protect access to all members except ready. */
	spinlock_t lock;
	/* lock to rotects access to ready flag. */
	spinlock_t ready_lock;
};

/**
 * struct scmi_vio_msg - Transport PDU information
 *
 * @request: SDU used for commands
 * @input: SDU used for (delayed) responses and notifications
 * @list: List which scmi_vio_msg may be part of
 * @rx_len: Input SDU size in bytes, once input has been received
 */
struct scmi_vio_msg {
	struct scmi_msg_payld *request;
	struct scmi_msg_payld *input;
	struct list_head list;
	unsigned int rx_len;
};

/* Only one SCMI VirtIO device can possibly exist */
static struct virtio_device *scmi_vdev;

static bool scmi_vio_have_vq_rx(struct virtio_device *vdev)
{
	return virtio_has_feature(vdev, VIRTIO_SCMI_F_P2A_CHANNELS);
}

static int scmi_vio_feed_vq_rx(struct scmi_vio_channel *vioch,
			       struct scmi_vio_msg *msg,
			       struct device *dev)
{
	struct scatterlist sg_in;
	int rc;
	unsigned long flags;

	sg_init_one(&sg_in, msg->input, VIRTIO_SCMI_MAX_PDU_SIZE);

	spin_lock_irqsave(&vioch->lock, flags);

	rc = virtqueue_add_inbuf(vioch->vqueue, &sg_in, 1, msg, GFP_ATOMIC);
	if (rc)
		dev_err_once(dev, "failed to add to virtqueue (%d)\n", rc);
	else
		virtqueue_kick(vioch->vqueue);

	spin_unlock_irqrestore(&vioch->lock, flags);

	return rc;
}

static void scmi_finalize_message(struct scmi_vio_channel *vioch,
				  struct scmi_vio_msg *msg)
{
	if (vioch->is_rx) {
		scmi_vio_feed_vq_rx(vioch, msg, vioch->cinfo->dev);
	} else {
		/* Here IRQs are assumed to be already disabled by the caller */
		spin_lock(&vioch->lock);
		list_add(&msg->list, &vioch->free_list);
		spin_unlock(&vioch->lock);
	}
}

static void scmi_vio_complete_cb(struct virtqueue *vqueue)
{
	unsigned long ready_flags;
	unsigned int length;
	struct scmi_vio_channel *vioch;
	struct scmi_vio_msg *msg;
	bool cb_enabled = true;

	if (WARN_ON_ONCE(!vqueue->vdev->priv))
		return;
	vioch = &((struct scmi_vio_channel *)vqueue->vdev->priv)[vqueue->index];

	for (;;) {
		spin_lock_irqsave(&vioch->ready_lock, ready_flags);

		if (!vioch->ready) {
			if (!cb_enabled)
				(void)virtqueue_enable_cb(vqueue);
			goto unlock_ready_out;
		}

		/* IRQs already disabled here no need to irqsave */
		spin_lock(&vioch->lock);
		if (cb_enabled) {
			virtqueue_disable_cb(vqueue);
			cb_enabled = false;
		}
		msg = virtqueue_get_buf(vqueue, &length);
		if (!msg) {
			if (virtqueue_enable_cb(vqueue))
				goto unlock_out;
			cb_enabled = true;
		}
		spin_unlock(&vioch->lock);

		if (msg) {
			msg->rx_len = length;
			scmi_rx_callback(vioch->cinfo,
					 msg_read_header(msg->input), msg);

			scmi_finalize_message(vioch, msg);
		}

		/*
		 * Release ready_lock and re-enable IRQs between loop iterations
		 * to allow virtio_chan_free() to possibly kick in and set the
		 * flag vioch->ready to false even in between processing of
		 * messages, so as to force outstanding messages to be ignored
		 * when system is shutting down.
		 */
		spin_unlock_irqrestore(&vioch->ready_lock, ready_flags);
	}

unlock_out:
	spin_unlock(&vioch->lock);
unlock_ready_out:
	spin_unlock_irqrestore(&vioch->ready_lock, ready_flags);
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
		dev_notice_once(dev,
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

static int virtio_chan_setup(struct scmi_chan_info *cinfo, struct device *dev,
			     bool tx)
{
	unsigned long flags;
	struct scmi_vio_channel *vioch;
	int index = tx ? VIRTIO_SCMI_VQ_TX : VIRTIO_SCMI_VQ_RX;
	int i;

	if (!scmi_vdev)
		return -EPROBE_DEFER;

	vioch = &((struct scmi_vio_channel *)scmi_vdev->priv)[index];

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
		}

		msg->input = devm_kzalloc(dev, VIRTIO_SCMI_MAX_PDU_SIZE,
					  GFP_KERNEL);
		if (!msg->input)
			return -ENOMEM;

		if (tx) {
			spin_lock_irqsave(&vioch->lock, flags);
			list_add_tail(&msg->list, &vioch->free_list);
			spin_unlock_irqrestore(&vioch->lock, flags);
		} else {
			scmi_vio_feed_vq_rx(vioch, msg, cinfo->dev);
		}
	}

	spin_lock_irqsave(&vioch->lock, flags);
	cinfo->transport_info = vioch;
	/* Indirectly setting channel not available any more */
	vioch->cinfo = cinfo;
	spin_unlock_irqrestore(&vioch->lock, flags);

	spin_lock_irqsave(&vioch->ready_lock, flags);
	vioch->ready = true;
	spin_unlock_irqrestore(&vioch->ready_lock, flags);

	return 0;
}

static int virtio_chan_free(int id, void *p, void *data)
{
	unsigned long flags;
	struct scmi_chan_info *cinfo = p;
	struct scmi_vio_channel *vioch = cinfo->transport_info;

	spin_lock_irqsave(&vioch->ready_lock, flags);
	vioch->ready = false;
	spin_unlock_irqrestore(&vioch->ready_lock, flags);

	scmi_free_channel(cinfo, data, id);

	spin_lock_irqsave(&vioch->lock, flags);
	vioch->cinfo = NULL;
	spin_unlock_irqrestore(&vioch->lock, flags);

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

	spin_lock_irqsave(&vioch->lock, flags);

	if (list_empty(&vioch->free_list)) {
		spin_unlock_irqrestore(&vioch->lock, flags);
		return -EBUSY;
	}

	msg = list_first_entry(&vioch->free_list, typeof(*msg), list);
	list_del(&msg->list);

	msg_tx_prepare(msg->request, xfer);

	sg_init_one(&sg_out, msg->request, msg_command_size(xfer));
	sg_init_one(&sg_in, msg->input, msg_response_size(xfer));

	rc = virtqueue_add_sgs(vioch->vqueue, sgs, 1, 1, msg, GFP_ATOMIC);
	if (rc) {
		list_add(&msg->list, &vioch->free_list);
		dev_err_once(vioch->cinfo->dev,
			     "%s() failed to add to virtqueue (%d)\n", __func__,
			     rc);
	} else {
		virtqueue_kick(vioch->vqueue);
	}

	spin_unlock_irqrestore(&vioch->lock, flags);

	return rc;
}

static void virtio_fetch_response(struct scmi_chan_info *cinfo,
				  struct scmi_xfer *xfer)
{
	struct scmi_vio_msg *msg = xfer->priv;

	if (msg) {
		msg_fetch_response(msg->input, msg->rx_len, xfer);
		xfer->priv = NULL;
	}
}

static void virtio_fetch_notification(struct scmi_chan_info *cinfo,
				      size_t max_len, struct scmi_xfer *xfer)
{
	struct scmi_vio_msg *msg = xfer->priv;

	if (msg) {
		msg_fetch_notification(msg->input, msg->rx_len, max_len, xfer);
		xfer->priv = NULL;
	}
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
		spin_lock_init(&channels[i].ready_lock);
		INIT_LIST_HEAD(&channels[i].free_list);
		channels[i].vqueue = vqs[i];

		sz = virtqueue_get_vring_size(channels[i].vqueue);
		/* Tx messages need multiple descriptors. */
		if (!channels[i].is_rx)
			sz /= DESCRIPTORS_PER_TX_MSG;

		if (sz > MSG_TOKEN_MAX) {
			dev_info_once(dev,
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
	vdev->config->reset(vdev);
	vdev->config->del_vqs(vdev);
	/* Ensure scmi_vdev is visible as NULL */
	smp_store_mb(scmi_vdev, NULL);
}

static int scmi_vio_validate(struct virtio_device *vdev)
{
	if (!virtio_has_feature(vdev, VIRTIO_F_VERSION_1)) {
		dev_err(&vdev->dev,
			"device does not comply with spec version 1.x\n");
		return -EINVAL;
	}

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
	.max_rx_timeout_ms = 60000, /* for non-realtime virtio devices */
	.max_msg = 0, /* overridden by virtio_get_max_msg() */
	.max_msg_size = VIRTIO_SCMI_MAX_MSG_SIZE,
};
