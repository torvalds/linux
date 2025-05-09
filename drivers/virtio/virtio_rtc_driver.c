// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * virtio_rtc driver core
 *
 * Copyright (C) 2022-2024 OpenSynergy GmbH
 * Copyright (c) 2024 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include <linux/completion.h>
#include <linux/device.h>
#include <linux/module.h>
#include <linux/pm.h>
#include <linux/virtio.h>
#include <linux/virtio_config.h>
#include <linux/virtio_ids.h>

#include <uapi/linux/virtio_rtc.h>

#include "virtio_rtc_internal.h"

/* virtqueue order */
enum {
	VIORTC_REQUESTQ,
	VIORTC_MAX_NR_QUEUES,
};

/**
 * struct viortc_vq - virtqueue abstraction
 * @vq: virtqueue
 * @lock: protects access to vq
 */
struct viortc_vq {
	struct virtqueue *vq;
	spinlock_t lock;
};

/**
 * struct viortc_dev - virtio_rtc device data
 * @vdev: virtio device
 * @vqs: virtqueues
 * @num_clocks: # of virtio_rtc clocks
 */
struct viortc_dev {
	struct virtio_device *vdev;
	struct viortc_vq vqs[VIORTC_MAX_NR_QUEUES];
	u16 num_clocks;
};

/**
 * struct viortc_msg - Message requested by driver, responded by device.
 * @viortc: device data
 * @req: request buffer
 * @resp: response buffer
 * @responded: vqueue callback signals response reception
 * @refcnt: Message reference count, message and buffers will be deallocated
 *	    once 0. refcnt is decremented in the vqueue callback and in the
 *	    thread waiting on the responded completion.
 *          If a message response wait function times out, the message will be
 *          freed upon late reception (refcnt will reach 0 in the callback), or
 *          device removal.
 * @req_size: size of request in bytes
 * @resp_cap: maximum size of response in bytes
 * @resp_actual_size: actual size of response
 */
struct viortc_msg {
	struct viortc_dev *viortc;
	void *req;
	void *resp;
	struct completion responded;
	refcount_t refcnt;
	unsigned int req_size;
	unsigned int resp_cap;
	unsigned int resp_actual_size;
};

/**
 * viortc_msg_init() - Allocate and initialize requestq message.
 * @viortc: device data
 * @msg_type: virtio_rtc message type
 * @req_size: size of request buffer to be allocated
 * @resp_cap: size of response buffer to be allocated
 *
 * Initializes the message refcnt to 2. The refcnt will be decremented once in
 * the virtqueue callback, and once in the thread waiting on the message (on
 * completion or timeout).
 *
 * Context: Process context.
 * Return: non-NULL on success.
 */
static struct viortc_msg *viortc_msg_init(struct viortc_dev *viortc,
					  u16 msg_type, unsigned int req_size,
					  unsigned int resp_cap)
{
	struct device *dev = &viortc->vdev->dev;
	struct virtio_rtc_req_head *req_head;
	struct viortc_msg *msg;

	msg = devm_kzalloc(dev, sizeof(*msg), GFP_KERNEL);
	if (!msg)
		return NULL;

	init_completion(&msg->responded);

	msg->req = devm_kzalloc(dev, req_size, GFP_KERNEL);
	if (!msg->req)
		goto err_free_msg;

	req_head = msg->req;

	msg->resp = devm_kzalloc(dev, resp_cap, GFP_KERNEL);
	if (!msg->resp)
		goto err_free_msg_req;

	msg->viortc = viortc;
	msg->req_size = req_size;
	msg->resp_cap = resp_cap;

	refcount_set(&msg->refcnt, 2);

	req_head->msg_type = virtio_cpu_to_le(msg_type, req_head->msg_type);

	return msg;

err_free_msg_req:
	devm_kfree(dev, msg->req);

err_free_msg:
	devm_kfree(dev, msg);

	return NULL;
}

/**
 * viortc_msg_release() - Decrement message refcnt, potentially free message.
 * @msg: message requested by driver
 *
 * Context: Any context.
 */
static void viortc_msg_release(struct viortc_msg *msg)
{
	struct device *dev;

	if (refcount_dec_and_test(&msg->refcnt)) {
		dev = &msg->viortc->vdev->dev;

		devm_kfree(dev, msg->req);
		devm_kfree(dev, msg->resp);
		devm_kfree(dev, msg);
	}
}

/**
 * viortc_do_cb() - generic virtqueue callback logic
 * @vq: virtqueue
 * @handle_buf: function to process a used buffer
 *
 * Context: virtqueue callback, typically interrupt. Takes and releases vq lock.
 */
static void viortc_do_cb(struct virtqueue *vq,
			 void (*handle_buf)(void *token, unsigned int len,
					    struct virtqueue *vq,
					    struct viortc_vq *viortc_vq,
					    struct viortc_dev *viortc))
{
	struct viortc_dev *viortc = vq->vdev->priv;
	struct viortc_vq *viortc_vq;
	bool cb_enabled = true;
	unsigned long flags;
	unsigned int len;
	void *token;

	viortc_vq = &viortc->vqs[vq->index];

	for (;;) {
		spin_lock_irqsave(&viortc_vq->lock, flags);

		if (cb_enabled) {
			virtqueue_disable_cb(vq);
			cb_enabled = false;
		}

		token = virtqueue_get_buf(vq, &len);
		if (!token) {
			if (virtqueue_enable_cb(vq)) {
				spin_unlock_irqrestore(&viortc_vq->lock, flags);
				return;
			}
			cb_enabled = true;
		}

		spin_unlock_irqrestore(&viortc_vq->lock, flags);

		if (token)
			handle_buf(token, len, vq, viortc_vq, viortc);
	}
}

/**
 * viortc_requestq_hdlr() - process a requestq used buffer
 * @token: token identifying the buffer
 * @len: bytes written by device
 * @vq: virtqueue
 * @viortc_vq: device specific data for virtqueue
 * @viortc: device data
 *
 * Signals completion for each received message.
 *
 * Context: virtqueue callback
 */
static void viortc_requestq_hdlr(void *token, unsigned int len,
				 struct virtqueue *vq,
				 struct viortc_vq *viortc_vq,
				 struct viortc_dev *viortc)
{
	struct viortc_msg *msg = token;

	msg->resp_actual_size = len;

	complete(&msg->responded);
	viortc_msg_release(msg);
}

/**
 * viortc_cb_requestq() - callback for requestq
 * @vq: virtqueue
 *
 * Context: virtqueue callback
 */
static void viortc_cb_requestq(struct virtqueue *vq)
{
	viortc_do_cb(vq, viortc_requestq_hdlr);
}

/**
 * viortc_get_resp_errno() - converts virtio_rtc errnos to system errnos
 * @resp_head: message response header
 *
 * Return: negative system errno, or 0
 */
static int viortc_get_resp_errno(struct virtio_rtc_resp_head *resp_head)
{
	switch (virtio_le_to_cpu(resp_head->status)) {
	case VIRTIO_RTC_S_OK:
		return 0;
	case VIRTIO_RTC_S_EOPNOTSUPP:
		return -EOPNOTSUPP;
	case VIRTIO_RTC_S_EINVAL:
		return -EINVAL;
	case VIRTIO_RTC_S_ENODEV:
		return -ENODEV;
	case VIRTIO_RTC_S_EIO:
	default:
		return -EIO;
	}
}

/**
 * viortc_msg_xfer() - send message request, wait until message response
 * @vq: virtqueue
 * @msg: message with driver request
 * @timeout_jiffies: message response timeout, 0 for no timeout
 *
 * Context: Process context. Takes and releases vq.lock. May sleep.
 * Return: Zero on success, negative error code otherwise.
 */
static int viortc_msg_xfer(struct viortc_vq *vq, struct viortc_msg *msg,
			   unsigned long timeout_jiffies)
{
	struct scatterlist out_sg[1];
	struct scatterlist in_sg[1];
	struct scatterlist *sgs[2];
	unsigned long flags;
	long timeout_ret;
	bool notify;
	int ret;

	sgs[0] = out_sg;
	sgs[1] = in_sg;

	sg_init_one(out_sg, msg->req, msg->req_size);
	sg_init_one(in_sg, msg->resp, msg->resp_cap);

	spin_lock_irqsave(&vq->lock, flags);

	ret = virtqueue_add_sgs(vq->vq, sgs, 1, 1, msg, GFP_ATOMIC);
	if (ret) {
		spin_unlock_irqrestore(&vq->lock, flags);
		/*
		 * Release in place of the response callback, which will never
		 * come.
		 */
		viortc_msg_release(msg);
		return ret;
	}

	notify = virtqueue_kick_prepare(vq->vq);

	spin_unlock_irqrestore(&vq->lock, flags);

	if (notify)
		virtqueue_notify(vq->vq);

	if (timeout_jiffies) {
		timeout_ret = wait_for_completion_interruptible_timeout(
			&msg->responded, timeout_jiffies);

		if (!timeout_ret)
			return -ETIMEDOUT;
		else if (timeout_ret < 0)
			return (int)timeout_ret;
	} else {
		ret = wait_for_completion_interruptible(&msg->responded);
		if (ret)
			return ret;
	}

	if (msg->resp_actual_size < sizeof(struct virtio_rtc_resp_head))
		return -EINVAL;

	ret = viortc_get_resp_errno(msg->resp);
	if (ret)
		return ret;

	/*
	 * There is not yet a case where returning a short message would make
	 * sense, so consider any deviation an error.
	 */
	if (msg->resp_actual_size != msg->resp_cap)
		return -EINVAL;

	return 0;
}

/*
 * common message handle macros for messages of different types
 */

/**
 * VIORTC_DECLARE_MSG_HDL_ONSTACK() - declare message handle on stack
 * @hdl: message handle name
 * @msg_id: message type id
 * @msg_req: message request type
 * @msg_resp: message response type
 */
#define VIORTC_DECLARE_MSG_HDL_ONSTACK(hdl, msg_id, msg_req, msg_resp)         \
	struct {                                                               \
		struct viortc_msg *msg;                                        \
		msg_req *req;                                                  \
		msg_resp *resp;                                                \
		unsigned int req_size;                                         \
		unsigned int resp_cap;                                         \
		u16 msg_type;                                                  \
	} hdl = {                                                              \
		NULL, NULL, NULL, sizeof(msg_req), sizeof(msg_resp), (msg_id), \
	}

/**
 * VIORTC_MSG() - extract message from message handle
 * @hdl: message handle
 *
 * Return: struct viortc_msg
 */
#define VIORTC_MSG(hdl) ((hdl).msg)

/**
 * VIORTC_MSG_INIT() - initialize message handle
 * @hdl: message handle
 * @viortc: device data (struct viortc_dev *)
 *
 * Context: Process context.
 * Return: 0 on success, -ENOMEM otherwise.
 */
#define VIORTC_MSG_INIT(hdl, viortc)                                         \
	({                                                                   \
		typeof(hdl) *_hdl = &(hdl);                                  \
									     \
		_hdl->msg = viortc_msg_init((viortc), _hdl->msg_type,        \
					    _hdl->req_size, _hdl->resp_cap); \
		if (_hdl->msg) {                                             \
			_hdl->req = _hdl->msg->req;                          \
			_hdl->resp = _hdl->msg->resp;                        \
		}                                                            \
		_hdl->msg ? 0 : -ENOMEM;                                     \
	})

/**
 * VIORTC_MSG_WRITE() - write a request message field
 * @hdl: message handle
 * @dest_member: request message field name
 * @src_ptr: pointer to data of compatible type
 *
 * Writes the field in little-endian format.
 */
#define VIORTC_MSG_WRITE(hdl, dest_member, src_ptr)                         \
	do {                                                                \
		typeof(hdl) _hdl = (hdl);                                   \
		typeof(src_ptr) _src_ptr = (src_ptr);                       \
									    \
		/* Sanity check: must match the member's type */            \
		typecheck(typeof(virtio_le_to_cpu(_hdl.req->dest_member)),  \
			  *_src_ptr);                                       \
									    \
		_hdl.req->dest_member =                                     \
			virtio_cpu_to_le(*_src_ptr, _hdl.req->dest_member); \
	} while (0)

/**
 * VIORTC_MSG_READ() - read from a response message field
 * @hdl: message handle
 * @src_member: response message field name
 * @dest_ptr: pointer to data of compatible type
 *
 * Converts from little-endian format and writes to dest_ptr.
 */
#define VIORTC_MSG_READ(hdl, src_member, dest_ptr)                          \
	do {                                                                \
		typeof(dest_ptr) _dest_ptr = (dest_ptr);                    \
									    \
		/* Sanity check: must match the member's type */            \
		typecheck(typeof(virtio_le_to_cpu((hdl).resp->src_member)), \
			  *_dest_ptr);                                      \
									    \
		*_dest_ptr = virtio_le_to_cpu((hdl).resp->src_member);      \
	} while (0)

/*
 * read requests
 */

/** timeout for clock readings, where timeouts are considered non-fatal */
#define VIORTC_MSG_READ_TIMEOUT secs_to_jiffies(60)

/**
 * viortc_read() - VIRTIO_RTC_REQ_READ wrapper
 * @viortc: device data
 * @vio_clk_id: virtio_rtc clock id
 * @reading: clock reading [ns]
 *
 * Context: Process context.
 * Return: Zero on success, negative error code otherwise.
 */
int viortc_read(struct viortc_dev *viortc, u16 vio_clk_id, u64 *reading)
{
	VIORTC_DECLARE_MSG_HDL_ONSTACK(hdl, VIRTIO_RTC_REQ_READ,
				       struct virtio_rtc_req_read,
				       struct virtio_rtc_resp_read);
	int ret;

	ret = VIORTC_MSG_INIT(hdl, viortc);
	if (ret)
		return ret;

	VIORTC_MSG_WRITE(hdl, clock_id, &vio_clk_id);

	ret = viortc_msg_xfer(&viortc->vqs[VIORTC_REQUESTQ], VIORTC_MSG(hdl),
			      VIORTC_MSG_READ_TIMEOUT);
	if (ret) {
		dev_dbg(&viortc->vdev->dev, "%s: xfer returned %d\n", __func__,
			ret);
		goto out_release;
	}

	VIORTC_MSG_READ(hdl, clock_reading, reading);

out_release:
	viortc_msg_release(VIORTC_MSG(hdl));

	return ret;
}

/**
 * viortc_read_cross() - VIRTIO_RTC_REQ_READ_CROSS wrapper
 * @viortc: device data
 * @vio_clk_id: virtio_rtc clock id
 * @hw_counter: virtio_rtc HW counter type
 * @reading: clock reading [ns]
 * @cycles: HW counter cycles during clock reading
 *
 * Context: Process context.
 * Return: Zero on success, negative error code otherwise.
 */
int viortc_read_cross(struct viortc_dev *viortc, u16 vio_clk_id, u8 hw_counter,
		      u64 *reading, u64 *cycles)
{
	VIORTC_DECLARE_MSG_HDL_ONSTACK(hdl, VIRTIO_RTC_REQ_READ_CROSS,
				       struct virtio_rtc_req_read_cross,
				       struct virtio_rtc_resp_read_cross);
	int ret;

	ret = VIORTC_MSG_INIT(hdl, viortc);
	if (ret)
		return ret;

	VIORTC_MSG_WRITE(hdl, clock_id, &vio_clk_id);
	VIORTC_MSG_WRITE(hdl, hw_counter, &hw_counter);

	ret = viortc_msg_xfer(&viortc->vqs[VIORTC_REQUESTQ], VIORTC_MSG(hdl),
			      VIORTC_MSG_READ_TIMEOUT);
	if (ret) {
		dev_dbg(&viortc->vdev->dev, "%s: xfer returned %d\n", __func__,
			ret);
		goto out_release;
	}

	VIORTC_MSG_READ(hdl, clock_reading, reading);
	VIORTC_MSG_READ(hdl, counter_cycles, cycles);

out_release:
	viortc_msg_release(VIORTC_MSG(hdl));

	return ret;
}

/*
 * control requests
 */

/**
 * viortc_cfg() - VIRTIO_RTC_REQ_CFG wrapper
 * @viortc: device data
 * @num_clocks: # of virtio_rtc clocks
 *
 * Context: Process context.
 * Return: Zero on success, negative error code otherwise.
 */
static int viortc_cfg(struct viortc_dev *viortc, u16 *num_clocks)
{
	VIORTC_DECLARE_MSG_HDL_ONSTACK(hdl, VIRTIO_RTC_REQ_CFG,
				       struct virtio_rtc_req_cfg,
				       struct virtio_rtc_resp_cfg);
	int ret;

	ret = VIORTC_MSG_INIT(hdl, viortc);
	if (ret)
		return ret;

	ret = viortc_msg_xfer(&viortc->vqs[VIORTC_REQUESTQ], VIORTC_MSG(hdl),
			      0);
	if (ret) {
		dev_dbg(&viortc->vdev->dev, "%s: xfer returned %d\n", __func__,
			ret);
		goto out_release;
	}

	VIORTC_MSG_READ(hdl, num_clocks, num_clocks);

out_release:
	viortc_msg_release(VIORTC_MSG(hdl));

	return ret;
}

/**
 * viortc_clock_cap() - VIRTIO_RTC_REQ_CLOCK_CAP wrapper
 * @viortc: device data
 * @vio_clk_id: virtio_rtc clock id
 * @type: virtio_rtc clock type
 * @leap_second_smearing: virtio_rtc smearing variant
 *
 * Context: Process context.
 * Return: Zero on success, negative error code otherwise.
 */
static int viortc_clock_cap(struct viortc_dev *viortc, u16 vio_clk_id, u8 *type,
			    u8 *leap_second_smearing)
{
	VIORTC_DECLARE_MSG_HDL_ONSTACK(hdl, VIRTIO_RTC_REQ_CLOCK_CAP,
				       struct virtio_rtc_req_clock_cap,
				       struct virtio_rtc_resp_clock_cap);
	int ret;

	ret = VIORTC_MSG_INIT(hdl, viortc);
	if (ret)
		return ret;

	VIORTC_MSG_WRITE(hdl, clock_id, &vio_clk_id);

	ret = viortc_msg_xfer(&viortc->vqs[VIORTC_REQUESTQ], VIORTC_MSG(hdl),
			      0);
	if (ret) {
		dev_dbg(&viortc->vdev->dev, "%s: xfer returned %d\n", __func__,
			ret);
		goto out_release;
	}

	VIORTC_MSG_READ(hdl, type, type);
	VIORTC_MSG_READ(hdl, leap_second_smearing, leap_second_smearing);

out_release:
	viortc_msg_release(VIORTC_MSG(hdl));

	return ret;
}

/**
 * viortc_cross_cap() - VIRTIO_RTC_REQ_CROSS_CAP wrapper
 * @viortc: device data
 * @vio_clk_id: virtio_rtc clock id
 * @hw_counter: virtio_rtc HW counter type
 * @supported: xtstamping is supported for the vio_clk_id/hw_counter pair
 *
 * Context: Process context.
 * Return: Zero on success, negative error code otherwise.
 */
int viortc_cross_cap(struct viortc_dev *viortc, u16 vio_clk_id, u8 hw_counter,
		     bool *supported)
{
	VIORTC_DECLARE_MSG_HDL_ONSTACK(hdl, VIRTIO_RTC_REQ_CROSS_CAP,
				       struct virtio_rtc_req_cross_cap,
				       struct virtio_rtc_resp_cross_cap);
	u8 flags;
	int ret;

	ret = VIORTC_MSG_INIT(hdl, viortc);
	if (ret)
		return ret;

	VIORTC_MSG_WRITE(hdl, clock_id, &vio_clk_id);
	VIORTC_MSG_WRITE(hdl, hw_counter, &hw_counter);

	ret = viortc_msg_xfer(&viortc->vqs[VIORTC_REQUESTQ], VIORTC_MSG(hdl),
			      0);
	if (ret) {
		dev_dbg(&viortc->vdev->dev, "%s: xfer returned %d\n", __func__,
			ret);
		goto out_release;
	}

	VIORTC_MSG_READ(hdl, flags, &flags);
	*supported = !!(flags & VIRTIO_RTC_FLAG_CROSS_CAP);

out_release:
	viortc_msg_release(VIORTC_MSG(hdl));

	return ret;
}

/*
 * init, deinit
 */

/**
 * viortc_clocks_init() - init local representations of virtio_rtc clocks
 * @viortc: device data
 *
 * Context: Process context.
 * Return: Zero on success, negative error code otherwise.
 */
static int viortc_clocks_init(struct viortc_dev *viortc)
{
	u16 num_clocks;
	int ret;

	ret = viortc_cfg(viortc, &num_clocks);
	if (ret)
		return ret;

	if (num_clocks < 1) {
		dev_err(&viortc->vdev->dev, "device reported 0 clocks\n");
		return -ENODEV;
	}

	viortc->num_clocks = num_clocks;

	/* In the future, PTP clocks will be initialized here. */
	(void)viortc_clock_cap;

	return ret;
}

/**
 * viortc_init_vqs() - init virtqueues
 * @viortc: device data
 *
 * Inits virtqueues and associated data.
 *
 * Context: Process context.
 * Return: Zero on success, negative error code otherwise.
 */
static int viortc_init_vqs(struct viortc_dev *viortc)
{
	struct virtqueue *vqs[VIORTC_MAX_NR_QUEUES];
	struct virtqueue_info vqs_info[] = {
		{ "requestq", viortc_cb_requestq },
	};
	struct virtio_device *vdev = viortc->vdev;
	int nr_queues, ret;

	nr_queues = VIORTC_REQUESTQ + 1;

	ret = virtio_find_vqs(vdev, nr_queues, vqs, vqs_info, NULL);
	if (ret)
		return ret;

	viortc->vqs[VIORTC_REQUESTQ].vq = vqs[VIORTC_REQUESTQ];
	spin_lock_init(&viortc->vqs[VIORTC_REQUESTQ].lock);

	return 0;
}

/**
 * viortc_probe() - probe a virtio_rtc virtio device
 * @vdev: virtio device
 *
 * Context: Process context.
 * Return: Zero on success, negative error code otherwise.
 */
static int viortc_probe(struct virtio_device *vdev)
{
	struct viortc_dev *viortc;
	int ret;

	viortc = devm_kzalloc(&vdev->dev, sizeof(*viortc), GFP_KERNEL);
	if (!viortc)
		return -ENOMEM;

	vdev->priv = viortc;
	viortc->vdev = vdev;

	ret = viortc_init_vqs(viortc);
	if (ret)
		return ret;

	virtio_device_ready(vdev);

	ret = viortc_clocks_init(viortc);
	if (ret)
		goto err_reset_vdev;

	return 0;

err_reset_vdev:
	virtio_reset_device(vdev);
	vdev->config->del_vqs(vdev);

	return ret;
}

/**
 * viortc_remove() - remove a virtio_rtc virtio device
 * @vdev: virtio device
 */
static void viortc_remove(struct virtio_device *vdev)
{
	/* In the future, PTP clocks will be deinitialized here. */

	virtio_reset_device(vdev);
	vdev->config->del_vqs(vdev);
}

static int viortc_freeze(struct virtio_device *dev)
{
	/*
	 * Do not reset the device, so that the device may still wake up the
	 * system through an alarmq notification.
	 */

	return 0;
}

static int viortc_restore(struct virtio_device *dev)
{
	struct viortc_dev *viortc = dev->priv;

	return viortc_init_vqs(viortc);
}

static struct virtio_device_id id_table[] = {
	{ VIRTIO_ID_CLOCK, VIRTIO_DEV_ANY_ID },
	{ 0 },
};
MODULE_DEVICE_TABLE(virtio, id_table);

static struct virtio_driver virtio_rtc_drv = {
	.driver.name = KBUILD_MODNAME,
	.id_table = id_table,
	.probe = viortc_probe,
	.remove = viortc_remove,
	.freeze = pm_sleep_ptr(viortc_freeze),
	.restore = pm_sleep_ptr(viortc_restore),
};

module_virtio_driver(virtio_rtc_drv);

MODULE_DESCRIPTION("Virtio RTC driver");
MODULE_AUTHOR("Qualcomm Innovation Center, Inc.");
MODULE_LICENSE("GPL");
