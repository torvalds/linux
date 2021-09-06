// SPDX-License-Identifier: GPL-2.0+
/*
 * Surface Book (gen. 2 and later) detachment system (DTX) driver.
 *
 * Provides a user-space interface to properly handle clipboard/tablet
 * (containing screen and processor) detachment from the base of the device
 * (containing the keyboard and optionally a discrete GPU). Allows to
 * acknowledge (to speed things up), abort (e.g. in case the dGPU is still in
 * use), or request detachment via user-space.
 *
 * Copyright (C) 2019-2021 Maximilian Luz <luzmaximilian@gmail.com>
 */

#include <linux/fs.h>
#include <linux/input.h>
#include <linux/ioctl.h>
#include <linux/kernel.h>
#include <linux/kfifo.h>
#include <linux/kref.h>
#include <linux/miscdevice.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/platform_device.h>
#include <linux/poll.h>
#include <linux/rwsem.h>
#include <linux/slab.h>
#include <linux/workqueue.h>

#include <linux/surface_aggregator/controller.h>
#include <linux/surface_aggregator/device.h>
#include <linux/surface_aggregator/dtx.h>


/* -- SSAM interface. ------------------------------------------------------- */

enum sam_event_cid_bas {
	SAM_EVENT_CID_DTX_CONNECTION			= 0x0c,
	SAM_EVENT_CID_DTX_REQUEST			= 0x0e,
	SAM_EVENT_CID_DTX_CANCEL			= 0x0f,
	SAM_EVENT_CID_DTX_LATCH_STATUS			= 0x11,
};

enum ssam_bas_base_state {
	SSAM_BAS_BASE_STATE_DETACH_SUCCESS		= 0x00,
	SSAM_BAS_BASE_STATE_ATTACHED			= 0x01,
	SSAM_BAS_BASE_STATE_NOT_FEASIBLE		= 0x02,
};

enum ssam_bas_latch_status {
	SSAM_BAS_LATCH_STATUS_CLOSED			= 0x00,
	SSAM_BAS_LATCH_STATUS_OPENED			= 0x01,
	SSAM_BAS_LATCH_STATUS_FAILED_TO_OPEN		= 0x02,
	SSAM_BAS_LATCH_STATUS_FAILED_TO_REMAIN_OPEN	= 0x03,
	SSAM_BAS_LATCH_STATUS_FAILED_TO_CLOSE		= 0x04,
};

enum ssam_bas_cancel_reason {
	SSAM_BAS_CANCEL_REASON_NOT_FEASIBLE		= 0x00,  /* Low battery. */
	SSAM_BAS_CANCEL_REASON_TIMEOUT			= 0x02,
	SSAM_BAS_CANCEL_REASON_FAILED_TO_OPEN		= 0x03,
	SSAM_BAS_CANCEL_REASON_FAILED_TO_REMAIN_OPEN	= 0x04,
	SSAM_BAS_CANCEL_REASON_FAILED_TO_CLOSE		= 0x05,
};

struct ssam_bas_base_info {
	u8 state;
	u8 base_id;
} __packed;

static_assert(sizeof(struct ssam_bas_base_info) == 2);

SSAM_DEFINE_SYNC_REQUEST_N(ssam_bas_latch_lock, {
	.target_category = SSAM_SSH_TC_BAS,
	.target_id       = 0x01,
	.command_id      = 0x06,
	.instance_id     = 0x00,
});

SSAM_DEFINE_SYNC_REQUEST_N(ssam_bas_latch_unlock, {
	.target_category = SSAM_SSH_TC_BAS,
	.target_id       = 0x01,
	.command_id      = 0x07,
	.instance_id     = 0x00,
});

SSAM_DEFINE_SYNC_REQUEST_N(ssam_bas_latch_request, {
	.target_category = SSAM_SSH_TC_BAS,
	.target_id       = 0x01,
	.command_id      = 0x08,
	.instance_id     = 0x00,
});

SSAM_DEFINE_SYNC_REQUEST_N(ssam_bas_latch_confirm, {
	.target_category = SSAM_SSH_TC_BAS,
	.target_id       = 0x01,
	.command_id      = 0x09,
	.instance_id     = 0x00,
});

SSAM_DEFINE_SYNC_REQUEST_N(ssam_bas_latch_heartbeat, {
	.target_category = SSAM_SSH_TC_BAS,
	.target_id       = 0x01,
	.command_id      = 0x0a,
	.instance_id     = 0x00,
});

SSAM_DEFINE_SYNC_REQUEST_N(ssam_bas_latch_cancel, {
	.target_category = SSAM_SSH_TC_BAS,
	.target_id       = 0x01,
	.command_id      = 0x0b,
	.instance_id     = 0x00,
});

SSAM_DEFINE_SYNC_REQUEST_R(ssam_bas_get_base, struct ssam_bas_base_info, {
	.target_category = SSAM_SSH_TC_BAS,
	.target_id       = 0x01,
	.command_id      = 0x0c,
	.instance_id     = 0x00,
});

SSAM_DEFINE_SYNC_REQUEST_R(ssam_bas_get_device_mode, u8, {
	.target_category = SSAM_SSH_TC_BAS,
	.target_id       = 0x01,
	.command_id      = 0x0d,
	.instance_id     = 0x00,
});

SSAM_DEFINE_SYNC_REQUEST_R(ssam_bas_get_latch_status, u8, {
	.target_category = SSAM_SSH_TC_BAS,
	.target_id       = 0x01,
	.command_id      = 0x11,
	.instance_id     = 0x00,
});


/* -- Main structures. ------------------------------------------------------ */

enum sdtx_device_state {
	SDTX_DEVICE_SHUTDOWN_BIT    = BIT(0),
	SDTX_DEVICE_DIRTY_BASE_BIT  = BIT(1),
	SDTX_DEVICE_DIRTY_MODE_BIT  = BIT(2),
	SDTX_DEVICE_DIRTY_LATCH_BIT = BIT(3),
};

struct sdtx_device {
	struct kref kref;
	struct rw_semaphore lock;         /* Guards device and controller reference. */

	struct device *dev;
	struct ssam_controller *ctrl;
	unsigned long flags;

	struct miscdevice mdev;
	wait_queue_head_t waitq;
	struct mutex write_lock;          /* Guards order of events/notifications. */
	struct rw_semaphore client_lock;  /* Guards client list.                   */
	struct list_head client_list;

	struct delayed_work state_work;
	struct {
		struct ssam_bas_base_info base;
		u8 device_mode;
		u8 latch_status;
	} state;

	struct delayed_work mode_work;
	struct input_dev *mode_switch;

	struct ssam_event_notifier notif;
};

enum sdtx_client_state {
	SDTX_CLIENT_EVENTS_ENABLED_BIT = BIT(0),
};

struct sdtx_client {
	struct sdtx_device *ddev;
	struct list_head node;
	unsigned long flags;

	struct fasync_struct *fasync;

	struct mutex read_lock;           /* Guards FIFO buffer read access. */
	DECLARE_KFIFO(buffer, u8, 512);
};

static void __sdtx_device_release(struct kref *kref)
{
	struct sdtx_device *ddev = container_of(kref, struct sdtx_device, kref);

	mutex_destroy(&ddev->write_lock);
	kfree(ddev);
}

static struct sdtx_device *sdtx_device_get(struct sdtx_device *ddev)
{
	if (ddev)
		kref_get(&ddev->kref);

	return ddev;
}

static void sdtx_device_put(struct sdtx_device *ddev)
{
	if (ddev)
		kref_put(&ddev->kref, __sdtx_device_release);
}


/* -- Firmware value translations. ------------------------------------------ */

static u16 sdtx_translate_base_state(struct sdtx_device *ddev, u8 state)
{
	switch (state) {
	case SSAM_BAS_BASE_STATE_ATTACHED:
		return SDTX_BASE_ATTACHED;

	case SSAM_BAS_BASE_STATE_DETACH_SUCCESS:
		return SDTX_BASE_DETACHED;

	case SSAM_BAS_BASE_STATE_NOT_FEASIBLE:
		return SDTX_DETACH_NOT_FEASIBLE;

	default:
		dev_err(ddev->dev, "unknown base state: %#04x\n", state);
		return SDTX_UNKNOWN(state);
	}
}

static u16 sdtx_translate_latch_status(struct sdtx_device *ddev, u8 status)
{
	switch (status) {
	case SSAM_BAS_LATCH_STATUS_CLOSED:
		return SDTX_LATCH_CLOSED;

	case SSAM_BAS_LATCH_STATUS_OPENED:
		return SDTX_LATCH_OPENED;

	case SSAM_BAS_LATCH_STATUS_FAILED_TO_OPEN:
		return SDTX_ERR_FAILED_TO_OPEN;

	case SSAM_BAS_LATCH_STATUS_FAILED_TO_REMAIN_OPEN:
		return SDTX_ERR_FAILED_TO_REMAIN_OPEN;

	case SSAM_BAS_LATCH_STATUS_FAILED_TO_CLOSE:
		return SDTX_ERR_FAILED_TO_CLOSE;

	default:
		dev_err(ddev->dev, "unknown latch status: %#04x\n", status);
		return SDTX_UNKNOWN(status);
	}
}

static u16 sdtx_translate_cancel_reason(struct sdtx_device *ddev, u8 reason)
{
	switch (reason) {
	case SSAM_BAS_CANCEL_REASON_NOT_FEASIBLE:
		return SDTX_DETACH_NOT_FEASIBLE;

	case SSAM_BAS_CANCEL_REASON_TIMEOUT:
		return SDTX_DETACH_TIMEDOUT;

	case SSAM_BAS_CANCEL_REASON_FAILED_TO_OPEN:
		return SDTX_ERR_FAILED_TO_OPEN;

	case SSAM_BAS_CANCEL_REASON_FAILED_TO_REMAIN_OPEN:
		return SDTX_ERR_FAILED_TO_REMAIN_OPEN;

	case SSAM_BAS_CANCEL_REASON_FAILED_TO_CLOSE:
		return SDTX_ERR_FAILED_TO_CLOSE;

	default:
		dev_err(ddev->dev, "unknown cancel reason: %#04x\n", reason);
		return SDTX_UNKNOWN(reason);
	}
}


/* -- IOCTLs. --------------------------------------------------------------- */

static int sdtx_ioctl_get_base_info(struct sdtx_device *ddev,
				    struct sdtx_base_info __user *buf)
{
	struct ssam_bas_base_info raw;
	struct sdtx_base_info info;
	int status;

	lockdep_assert_held_read(&ddev->lock);

	status = ssam_retry(ssam_bas_get_base, ddev->ctrl, &raw);
	if (status < 0)
		return status;

	info.state = sdtx_translate_base_state(ddev, raw.state);
	info.base_id = SDTX_BASE_TYPE_SSH(raw.base_id);

	if (copy_to_user(buf, &info, sizeof(info)))
		return -EFAULT;

	return 0;
}

static int sdtx_ioctl_get_device_mode(struct sdtx_device *ddev, u16 __user *buf)
{
	u8 mode;
	int status;

	lockdep_assert_held_read(&ddev->lock);

	status = ssam_retry(ssam_bas_get_device_mode, ddev->ctrl, &mode);
	if (status < 0)
		return status;

	return put_user(mode, buf);
}

static int sdtx_ioctl_get_latch_status(struct sdtx_device *ddev, u16 __user *buf)
{
	u8 latch;
	int status;

	lockdep_assert_held_read(&ddev->lock);

	status = ssam_retry(ssam_bas_get_latch_status, ddev->ctrl, &latch);
	if (status < 0)
		return status;

	return put_user(sdtx_translate_latch_status(ddev, latch), buf);
}

static long __surface_dtx_ioctl(struct sdtx_client *client, unsigned int cmd, unsigned long arg)
{
	struct sdtx_device *ddev = client->ddev;

	lockdep_assert_held_read(&ddev->lock);

	switch (cmd) {
	case SDTX_IOCTL_EVENTS_ENABLE:
		set_bit(SDTX_CLIENT_EVENTS_ENABLED_BIT, &client->flags);
		return 0;

	case SDTX_IOCTL_EVENTS_DISABLE:
		clear_bit(SDTX_CLIENT_EVENTS_ENABLED_BIT, &client->flags);
		return 0;

	case SDTX_IOCTL_LATCH_LOCK:
		return ssam_retry(ssam_bas_latch_lock, ddev->ctrl);

	case SDTX_IOCTL_LATCH_UNLOCK:
		return ssam_retry(ssam_bas_latch_unlock, ddev->ctrl);

	case SDTX_IOCTL_LATCH_REQUEST:
		return ssam_retry(ssam_bas_latch_request, ddev->ctrl);

	case SDTX_IOCTL_LATCH_CONFIRM:
		return ssam_retry(ssam_bas_latch_confirm, ddev->ctrl);

	case SDTX_IOCTL_LATCH_HEARTBEAT:
		return ssam_retry(ssam_bas_latch_heartbeat, ddev->ctrl);

	case SDTX_IOCTL_LATCH_CANCEL:
		return ssam_retry(ssam_bas_latch_cancel, ddev->ctrl);

	case SDTX_IOCTL_GET_BASE_INFO:
		return sdtx_ioctl_get_base_info(ddev, (struct sdtx_base_info __user *)arg);

	case SDTX_IOCTL_GET_DEVICE_MODE:
		return sdtx_ioctl_get_device_mode(ddev, (u16 __user *)arg);

	case SDTX_IOCTL_GET_LATCH_STATUS:
		return sdtx_ioctl_get_latch_status(ddev, (u16 __user *)arg);

	default:
		return -EINVAL;
	}
}

static long surface_dtx_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	struct sdtx_client *client = file->private_data;
	long status;

	if (down_read_killable(&client->ddev->lock))
		return -ERESTARTSYS;

	if (test_bit(SDTX_DEVICE_SHUTDOWN_BIT, &client->ddev->flags)) {
		up_read(&client->ddev->lock);
		return -ENODEV;
	}

	status = __surface_dtx_ioctl(client, cmd, arg);

	up_read(&client->ddev->lock);
	return status;
}


/* -- File operations. ------------------------------------------------------ */

static int surface_dtx_open(struct inode *inode, struct file *file)
{
	struct sdtx_device *ddev = container_of(file->private_data, struct sdtx_device, mdev);
	struct sdtx_client *client;

	/* Initialize client. */
	client = kzalloc(sizeof(*client), GFP_KERNEL);
	if (!client)
		return -ENOMEM;

	client->ddev = sdtx_device_get(ddev);

	INIT_LIST_HEAD(&client->node);

	mutex_init(&client->read_lock);
	INIT_KFIFO(client->buffer);

	file->private_data = client;

	/* Attach client. */
	down_write(&ddev->client_lock);

	/*
	 * Do not add a new client if the device has been shut down. Note that
	 * it's enough to hold the client_lock here as, during shutdown, we
	 * only acquire that lock and remove clients after marking the device
	 * as shut down.
	 */
	if (test_bit(SDTX_DEVICE_SHUTDOWN_BIT, &ddev->flags)) {
		up_write(&ddev->client_lock);
		mutex_destroy(&client->read_lock);
		sdtx_device_put(client->ddev);
		kfree(client);
		return -ENODEV;
	}

	list_add_tail(&client->node, &ddev->client_list);
	up_write(&ddev->client_lock);

	stream_open(inode, file);
	return 0;
}

static int surface_dtx_release(struct inode *inode, struct file *file)
{
	struct sdtx_client *client = file->private_data;

	/* Detach client. */
	down_write(&client->ddev->client_lock);
	list_del(&client->node);
	up_write(&client->ddev->client_lock);

	/* Free client. */
	sdtx_device_put(client->ddev);
	mutex_destroy(&client->read_lock);
	kfree(client);

	return 0;
}

static ssize_t surface_dtx_read(struct file *file, char __user *buf, size_t count, loff_t *offs)
{
	struct sdtx_client *client = file->private_data;
	struct sdtx_device *ddev = client->ddev;
	unsigned int copied;
	int status = 0;

	if (down_read_killable(&ddev->lock))
		return -ERESTARTSYS;

	/* Make sure we're not shut down. */
	if (test_bit(SDTX_DEVICE_SHUTDOWN_BIT, &ddev->flags)) {
		up_read(&ddev->lock);
		return -ENODEV;
	}

	do {
		/* Check availability, wait if necessary. */
		if (kfifo_is_empty(&client->buffer)) {
			up_read(&ddev->lock);

			if (file->f_flags & O_NONBLOCK)
				return -EAGAIN;

			status = wait_event_interruptible(ddev->waitq,
							  !kfifo_is_empty(&client->buffer) ||
							  test_bit(SDTX_DEVICE_SHUTDOWN_BIT,
								   &ddev->flags));
			if (status < 0)
				return status;

			if (down_read_killable(&ddev->lock))
				return -ERESTARTSYS;

			/* Need to check that we're not shut down again. */
			if (test_bit(SDTX_DEVICE_SHUTDOWN_BIT, &ddev->flags)) {
				up_read(&ddev->lock);
				return -ENODEV;
			}
		}

		/* Try to read from FIFO. */
		if (mutex_lock_interruptible(&client->read_lock)) {
			up_read(&ddev->lock);
			return -ERESTARTSYS;
		}

		status = kfifo_to_user(&client->buffer, buf, count, &copied);
		mutex_unlock(&client->read_lock);

		if (status < 0) {
			up_read(&ddev->lock);
			return status;
		}

		/* We might not have gotten anything, check this here. */
		if (copied == 0 && (file->f_flags & O_NONBLOCK)) {
			up_read(&ddev->lock);
			return -EAGAIN;
		}
	} while (copied == 0);

	up_read(&ddev->lock);
	return copied;
}

static __poll_t surface_dtx_poll(struct file *file, struct poll_table_struct *pt)
{
	struct sdtx_client *client = file->private_data;
	__poll_t events = 0;

	if (test_bit(SDTX_DEVICE_SHUTDOWN_BIT, &client->ddev->flags))
		return EPOLLHUP | EPOLLERR;

	poll_wait(file, &client->ddev->waitq, pt);

	if (!kfifo_is_empty(&client->buffer))
		events |= EPOLLIN | EPOLLRDNORM;

	return events;
}

static int surface_dtx_fasync(int fd, struct file *file, int on)
{
	struct sdtx_client *client = file->private_data;

	return fasync_helper(fd, file, on, &client->fasync);
}

static const struct file_operations surface_dtx_fops = {
	.owner          = THIS_MODULE,
	.open           = surface_dtx_open,
	.release        = surface_dtx_release,
	.read           = surface_dtx_read,
	.poll           = surface_dtx_poll,
	.fasync         = surface_dtx_fasync,
	.unlocked_ioctl = surface_dtx_ioctl,
	.compat_ioctl   = surface_dtx_ioctl,
	.llseek         = no_llseek,
};


/* -- Event handling/forwarding. -------------------------------------------- */

/*
 * The device operation mode is not immediately updated on the EC when the
 * base has been connected, i.e. querying the device mode inside the
 * connection event callback yields an outdated value. Thus, we can only
 * determine the new tablet-mode switch and device mode values after some
 * time.
 *
 * These delays have been chosen by experimenting. We first delay on connect
 * events, then check and validate the device mode against the base state and
 * if invalid delay again by the "recheck" delay.
 */
#define SDTX_DEVICE_MODE_DELAY_CONNECT	msecs_to_jiffies(100)
#define SDTX_DEVICE_MODE_DELAY_RECHECK	msecs_to_jiffies(100)

struct sdtx_status_event {
	struct sdtx_event e;
	__u16 v;
} __packed;

struct sdtx_base_info_event {
	struct sdtx_event e;
	struct sdtx_base_info v;
} __packed;

union sdtx_generic_event {
	struct sdtx_event common;
	struct sdtx_status_event status;
	struct sdtx_base_info_event base;
};

static void sdtx_update_device_mode(struct sdtx_device *ddev, unsigned long delay);

/* Must be executed with ddev->write_lock held. */
static void sdtx_push_event(struct sdtx_device *ddev, struct sdtx_event *evt)
{
	const size_t len = sizeof(struct sdtx_event) + evt->length;
	struct sdtx_client *client;

	lockdep_assert_held(&ddev->write_lock);

	down_read(&ddev->client_lock);
	list_for_each_entry(client, &ddev->client_list, node) {
		if (!test_bit(SDTX_CLIENT_EVENTS_ENABLED_BIT, &client->flags))
			continue;

		if (likely(kfifo_avail(&client->buffer) >= len))
			kfifo_in(&client->buffer, (const u8 *)evt, len);
		else
			dev_warn(ddev->dev, "event buffer overrun\n");

		kill_fasync(&client->fasync, SIGIO, POLL_IN);
	}
	up_read(&ddev->client_lock);

	wake_up_interruptible(&ddev->waitq);
}

static u32 sdtx_notifier(struct ssam_event_notifier *nf, const struct ssam_event *in)
{
	struct sdtx_device *ddev = container_of(nf, struct sdtx_device, notif);
	union sdtx_generic_event event;
	size_t len;

	/* Validate event payload length. */
	switch (in->command_id) {
	case SAM_EVENT_CID_DTX_CONNECTION:
		len = 2 * sizeof(u8);
		break;

	case SAM_EVENT_CID_DTX_REQUEST:
		len = 0;
		break;

	case SAM_EVENT_CID_DTX_CANCEL:
		len = sizeof(u8);
		break;

	case SAM_EVENT_CID_DTX_LATCH_STATUS:
		len = sizeof(u8);
		break;

	default:
		return 0;
	}

	if (in->length != len) {
		dev_err(ddev->dev,
			"unexpected payload size for event %#04x: got %u, expected %zu\n",
			in->command_id, in->length, len);
		return 0;
	}

	mutex_lock(&ddev->write_lock);

	/* Translate event. */
	switch (in->command_id) {
	case SAM_EVENT_CID_DTX_CONNECTION:
		clear_bit(SDTX_DEVICE_DIRTY_BASE_BIT, &ddev->flags);

		/* If state has not changed: do not send new event. */
		if (ddev->state.base.state == in->data[0] &&
		    ddev->state.base.base_id == in->data[1])
			goto out;

		ddev->state.base.state = in->data[0];
		ddev->state.base.base_id = in->data[1];

		event.base.e.length = sizeof(struct sdtx_base_info);
		event.base.e.code = SDTX_EVENT_BASE_CONNECTION;
		event.base.v.state = sdtx_translate_base_state(ddev, in->data[0]);
		event.base.v.base_id = SDTX_BASE_TYPE_SSH(in->data[1]);
		break;

	case SAM_EVENT_CID_DTX_REQUEST:
		event.common.code = SDTX_EVENT_REQUEST;
		event.common.length = 0;
		break;

	case SAM_EVENT_CID_DTX_CANCEL:
		event.status.e.length = sizeof(u16);
		event.status.e.code = SDTX_EVENT_CANCEL;
		event.status.v = sdtx_translate_cancel_reason(ddev, in->data[0]);
		break;

	case SAM_EVENT_CID_DTX_LATCH_STATUS:
		clear_bit(SDTX_DEVICE_DIRTY_LATCH_BIT, &ddev->flags);

		/* If state has not changed: do not send new event. */
		if (ddev->state.latch_status == in->data[0])
			goto out;

		ddev->state.latch_status = in->data[0];

		event.status.e.length = sizeof(u16);
		event.status.e.code = SDTX_EVENT_LATCH_STATUS;
		event.status.v = sdtx_translate_latch_status(ddev, in->data[0]);
		break;
	}

	sdtx_push_event(ddev, &event.common);

	/* Update device mode on base connection change. */
	if (in->command_id == SAM_EVENT_CID_DTX_CONNECTION) {
		unsigned long delay;

		delay = in->data[0] ? SDTX_DEVICE_MODE_DELAY_CONNECT : 0;
		sdtx_update_device_mode(ddev, delay);
	}

out:
	mutex_unlock(&ddev->write_lock);
	return SSAM_NOTIF_HANDLED;
}


/* -- State update functions. ----------------------------------------------- */

static bool sdtx_device_mode_invalid(u8 mode, u8 base_state)
{
	return ((base_state == SSAM_BAS_BASE_STATE_ATTACHED) &&
		(mode == SDTX_DEVICE_MODE_TABLET)) ||
	       ((base_state == SSAM_BAS_BASE_STATE_DETACH_SUCCESS) &&
		(mode != SDTX_DEVICE_MODE_TABLET));
}

static void sdtx_device_mode_workfn(struct work_struct *work)
{
	struct sdtx_device *ddev = container_of(work, struct sdtx_device, mode_work.work);
	struct sdtx_status_event event;
	struct ssam_bas_base_info base;
	int status, tablet;
	u8 mode;

	/* Get operation mode. */
	status = ssam_retry(ssam_bas_get_device_mode, ddev->ctrl, &mode);
	if (status) {
		dev_err(ddev->dev, "failed to get device mode: %d\n", status);
		return;
	}

	/* Get base info. */
	status = ssam_retry(ssam_bas_get_base, ddev->ctrl, &base);
	if (status) {
		dev_err(ddev->dev, "failed to get base info: %d\n", status);
		return;
	}

	/*
	 * In some cases (specifically when attaching the base), the device
	 * mode isn't updated right away. Thus we check if the device mode
	 * makes sense for the given base state and try again later if it
	 * doesn't.
	 */
	if (sdtx_device_mode_invalid(mode, base.state)) {
		dev_dbg(ddev->dev, "device mode is invalid, trying again\n");
		sdtx_update_device_mode(ddev, SDTX_DEVICE_MODE_DELAY_RECHECK);
		return;
	}

	mutex_lock(&ddev->write_lock);
	clear_bit(SDTX_DEVICE_DIRTY_MODE_BIT, &ddev->flags);

	/* Avoid sending duplicate device-mode events. */
	if (ddev->state.device_mode == mode) {
		mutex_unlock(&ddev->write_lock);
		return;
	}

	ddev->state.device_mode = mode;

	event.e.length = sizeof(u16);
	event.e.code = SDTX_EVENT_DEVICE_MODE;
	event.v = mode;

	sdtx_push_event(ddev, &event.e);

	/* Send SW_TABLET_MODE event. */
	tablet = mode != SDTX_DEVICE_MODE_LAPTOP;
	input_report_switch(ddev->mode_switch, SW_TABLET_MODE, tablet);
	input_sync(ddev->mode_switch);

	mutex_unlock(&ddev->write_lock);
}

static void sdtx_update_device_mode(struct sdtx_device *ddev, unsigned long delay)
{
	schedule_delayed_work(&ddev->mode_work, delay);
}

/* Must be executed with ddev->write_lock held. */
static void __sdtx_device_state_update_base(struct sdtx_device *ddev,
					    struct ssam_bas_base_info info)
{
	struct sdtx_base_info_event event;

	lockdep_assert_held(&ddev->write_lock);

	/* Prevent duplicate events. */
	if (ddev->state.base.state == info.state &&
	    ddev->state.base.base_id == info.base_id)
		return;

	ddev->state.base = info;

	event.e.length = sizeof(struct sdtx_base_info);
	event.e.code = SDTX_EVENT_BASE_CONNECTION;
	event.v.state = sdtx_translate_base_state(ddev, info.state);
	event.v.base_id = SDTX_BASE_TYPE_SSH(info.base_id);

	sdtx_push_event(ddev, &event.e);
}

/* Must be executed with ddev->write_lock held. */
static void __sdtx_device_state_update_mode(struct sdtx_device *ddev, u8 mode)
{
	struct sdtx_status_event event;
	int tablet;

	/*
	 * Note: This function must be called after updating the base state
	 * via __sdtx_device_state_update_base(), as we rely on the updated
	 * base state value in the validity check below.
	 */

	lockdep_assert_held(&ddev->write_lock);

	if (sdtx_device_mode_invalid(mode, ddev->state.base.state)) {
		dev_dbg(ddev->dev, "device mode is invalid, trying again\n");
		sdtx_update_device_mode(ddev, SDTX_DEVICE_MODE_DELAY_RECHECK);
		return;
	}

	/* Prevent duplicate events. */
	if (ddev->state.device_mode == mode)
		return;

	ddev->state.device_mode = mode;

	/* Send event. */
	event.e.length = sizeof(u16);
	event.e.code = SDTX_EVENT_DEVICE_MODE;
	event.v = mode;

	sdtx_push_event(ddev, &event.e);

	/* Send SW_TABLET_MODE event. */
	tablet = mode != SDTX_DEVICE_MODE_LAPTOP;
	input_report_switch(ddev->mode_switch, SW_TABLET_MODE, tablet);
	input_sync(ddev->mode_switch);
}

/* Must be executed with ddev->write_lock held. */
static void __sdtx_device_state_update_latch(struct sdtx_device *ddev, u8 status)
{
	struct sdtx_status_event event;

	lockdep_assert_held(&ddev->write_lock);

	/* Prevent duplicate events. */
	if (ddev->state.latch_status == status)
		return;

	ddev->state.latch_status = status;

	event.e.length = sizeof(struct sdtx_base_info);
	event.e.code = SDTX_EVENT_BASE_CONNECTION;
	event.v = sdtx_translate_latch_status(ddev, status);

	sdtx_push_event(ddev, &event.e);
}

static void sdtx_device_state_workfn(struct work_struct *work)
{
	struct sdtx_device *ddev = container_of(work, struct sdtx_device, state_work.work);
	struct ssam_bas_base_info base;
	u8 mode, latch;
	int status;

	/* Mark everything as dirty. */
	set_bit(SDTX_DEVICE_DIRTY_BASE_BIT, &ddev->flags);
	set_bit(SDTX_DEVICE_DIRTY_MODE_BIT, &ddev->flags);
	set_bit(SDTX_DEVICE_DIRTY_LATCH_BIT, &ddev->flags);

	/*
	 * Ensure that the state gets marked as dirty before continuing to
	 * query it. Necessary to ensure that clear_bit() calls in
	 * sdtx_notifier() and sdtx_device_mode_workfn() actually clear these
	 * bits if an event is received while updating the state here.
	 */
	smp_mb__after_atomic();

	status = ssam_retry(ssam_bas_get_base, ddev->ctrl, &base);
	if (status) {
		dev_err(ddev->dev, "failed to get base state: %d\n", status);
		return;
	}

	status = ssam_retry(ssam_bas_get_device_mode, ddev->ctrl, &mode);
	if (status) {
		dev_err(ddev->dev, "failed to get device mode: %d\n", status);
		return;
	}

	status = ssam_retry(ssam_bas_get_latch_status, ddev->ctrl, &latch);
	if (status) {
		dev_err(ddev->dev, "failed to get latch status: %d\n", status);
		return;
	}

	mutex_lock(&ddev->write_lock);

	/*
	 * If the respective dirty-bit has been cleared, an event has been
	 * received, updating this state. The queried state may thus be out of
	 * date. At this point, we can safely assume that the state provided
	 * by the event is either up to date, or we're about to receive
	 * another event updating it.
	 */

	if (test_and_clear_bit(SDTX_DEVICE_DIRTY_BASE_BIT, &ddev->flags))
		__sdtx_device_state_update_base(ddev, base);

	if (test_and_clear_bit(SDTX_DEVICE_DIRTY_MODE_BIT, &ddev->flags))
		__sdtx_device_state_update_mode(ddev, mode);

	if (test_and_clear_bit(SDTX_DEVICE_DIRTY_LATCH_BIT, &ddev->flags))
		__sdtx_device_state_update_latch(ddev, latch);

	mutex_unlock(&ddev->write_lock);
}

static void sdtx_update_device_state(struct sdtx_device *ddev, unsigned long delay)
{
	schedule_delayed_work(&ddev->state_work, delay);
}


/* -- Common device initialization. ----------------------------------------- */

static int sdtx_device_init(struct sdtx_device *ddev, struct device *dev,
			    struct ssam_controller *ctrl)
{
	int status, tablet_mode;

	/* Basic initialization. */
	kref_init(&ddev->kref);
	init_rwsem(&ddev->lock);
	ddev->dev = dev;
	ddev->ctrl = ctrl;

	ddev->mdev.minor = MISC_DYNAMIC_MINOR;
	ddev->mdev.name = "surface_dtx";
	ddev->mdev.nodename = "surface/dtx";
	ddev->mdev.fops = &surface_dtx_fops;

	ddev->notif.base.priority = 1;
	ddev->notif.base.fn = sdtx_notifier;
	ddev->notif.event.reg = SSAM_EVENT_REGISTRY_SAM;
	ddev->notif.event.id.target_category = SSAM_SSH_TC_BAS;
	ddev->notif.event.id.instance = 0;
	ddev->notif.event.mask = SSAM_EVENT_MASK_NONE;
	ddev->notif.event.flags = SSAM_EVENT_SEQUENCED;

	init_waitqueue_head(&ddev->waitq);
	mutex_init(&ddev->write_lock);
	init_rwsem(&ddev->client_lock);
	INIT_LIST_HEAD(&ddev->client_list);

	INIT_DELAYED_WORK(&ddev->mode_work, sdtx_device_mode_workfn);
	INIT_DELAYED_WORK(&ddev->state_work, sdtx_device_state_workfn);

	/*
	 * Get current device state. We want to guarantee that events are only
	 * sent when state actually changes. Thus we cannot use special
	 * "uninitialized" values, as that would cause problems when manually
	 * querying the state in surface_dtx_pm_complete(). I.e. we would not
	 * be able to detect state changes there if no change event has been
	 * received between driver initialization and first device suspension.
	 *
	 * Note that we also need to do this before registering the event
	 * notifier, as that may access the state values.
	 */
	status = ssam_retry(ssam_bas_get_base, ddev->ctrl, &ddev->state.base);
	if (status)
		return status;

	status = ssam_retry(ssam_bas_get_device_mode, ddev->ctrl, &ddev->state.device_mode);
	if (status)
		return status;

	status = ssam_retry(ssam_bas_get_latch_status, ddev->ctrl, &ddev->state.latch_status);
	if (status)
		return status;

	/* Set up tablet mode switch. */
	ddev->mode_switch = input_allocate_device();
	if (!ddev->mode_switch)
		return -ENOMEM;

	ddev->mode_switch->name = "Microsoft Surface DTX Device Mode Switch";
	ddev->mode_switch->phys = "ssam/01:11:01:00:00/input0";
	ddev->mode_switch->id.bustype = BUS_HOST;
	ddev->mode_switch->dev.parent = ddev->dev;

	tablet_mode = (ddev->state.device_mode != SDTX_DEVICE_MODE_LAPTOP);
	input_set_capability(ddev->mode_switch, EV_SW, SW_TABLET_MODE);
	input_report_switch(ddev->mode_switch, SW_TABLET_MODE, tablet_mode);

	status = input_register_device(ddev->mode_switch);
	if (status) {
		input_free_device(ddev->mode_switch);
		return status;
	}

	/* Set up event notifier. */
	status = ssam_notifier_register(ddev->ctrl, &ddev->notif);
	if (status)
		goto err_notif;

	/* Register miscdevice. */
	status = misc_register(&ddev->mdev);
	if (status)
		goto err_mdev;

	/*
	 * Update device state in case it has changed between getting the
	 * initial mode and registering the event notifier.
	 */
	sdtx_update_device_state(ddev, 0);
	return 0;

err_notif:
	ssam_notifier_unregister(ddev->ctrl, &ddev->notif);
	cancel_delayed_work_sync(&ddev->mode_work);
err_mdev:
	input_unregister_device(ddev->mode_switch);
	return status;
}

static struct sdtx_device *sdtx_device_create(struct device *dev, struct ssam_controller *ctrl)
{
	struct sdtx_device *ddev;
	int status;

	ddev = kzalloc(sizeof(*ddev), GFP_KERNEL);
	if (!ddev)
		return ERR_PTR(-ENOMEM);

	status = sdtx_device_init(ddev, dev, ctrl);
	if (status) {
		sdtx_device_put(ddev);
		return ERR_PTR(status);
	}

	return ddev;
}

static void sdtx_device_destroy(struct sdtx_device *ddev)
{
	struct sdtx_client *client;

	/*
	 * Mark device as shut-down. Prevent new clients from being added and
	 * new operations from being executed.
	 */
	set_bit(SDTX_DEVICE_SHUTDOWN_BIT, &ddev->flags);

	/* Disable notifiers, prevent new events from arriving. */
	ssam_notifier_unregister(ddev->ctrl, &ddev->notif);

	/* Stop mode_work, prevent access to mode_switch. */
	cancel_delayed_work_sync(&ddev->mode_work);

	/* Stop state_work. */
	cancel_delayed_work_sync(&ddev->state_work);

	/* With mode_work canceled, we can unregister the mode_switch. */
	input_unregister_device(ddev->mode_switch);

	/* Wake up async clients. */
	down_write(&ddev->client_lock);
	list_for_each_entry(client, &ddev->client_list, node) {
		kill_fasync(&client->fasync, SIGIO, POLL_HUP);
	}
	up_write(&ddev->client_lock);

	/* Wake up blocking clients. */
	wake_up_interruptible(&ddev->waitq);

	/*
	 * Wait for clients to finish their current operation. After this, the
	 * controller and device references are guaranteed to be no longer in
	 * use.
	 */
	down_write(&ddev->lock);
	ddev->dev = NULL;
	ddev->ctrl = NULL;
	up_write(&ddev->lock);

	/* Finally remove the misc-device. */
	misc_deregister(&ddev->mdev);

	/*
	 * We're now guaranteed that sdtx_device_open() won't be called any
	 * more, so we can now drop out reference.
	 */
	sdtx_device_put(ddev);
}


/* -- PM ops. --------------------------------------------------------------- */

#ifdef CONFIG_PM_SLEEP

static void surface_dtx_pm_complete(struct device *dev)
{
	struct sdtx_device *ddev = dev_get_drvdata(dev);

	/*
	 * Normally, the EC will store events while suspended (i.e. in
	 * display-off state) and release them when resumed (i.e. transitioned
	 * to display-on state). During hibernation, however, the EC will be
	 * shut down and does not store events. Furthermore, events might be
	 * dropped during prolonged suspension (it is currently unknown how
	 * big this event buffer is and how it behaves on overruns).
	 *
	 * To prevent any problems, we update the device state here. We do
	 * this delayed to ensure that any events sent by the EC directly
	 * after resuming will be handled first. The delay below has been
	 * chosen (experimentally), so that there should be ample time for
	 * these events to be handled, before we check and, if necessary,
	 * update the state.
	 */
	sdtx_update_device_state(ddev, msecs_to_jiffies(1000));
}

static const struct dev_pm_ops surface_dtx_pm_ops = {
	.complete = surface_dtx_pm_complete,
};

#else /* CONFIG_PM_SLEEP */

static const struct dev_pm_ops surface_dtx_pm_ops = {};

#endif /* CONFIG_PM_SLEEP */


/* -- Platform driver. ------------------------------------------------------ */

static int surface_dtx_platform_probe(struct platform_device *pdev)
{
	struct ssam_controller *ctrl;
	struct sdtx_device *ddev;

	/* Link to EC. */
	ctrl = ssam_client_bind(&pdev->dev);
	if (IS_ERR(ctrl))
		return PTR_ERR(ctrl) == -ENODEV ? -EPROBE_DEFER : PTR_ERR(ctrl);

	ddev = sdtx_device_create(&pdev->dev, ctrl);
	if (IS_ERR(ddev))
		return PTR_ERR(ddev);

	platform_set_drvdata(pdev, ddev);
	return 0;
}

static int surface_dtx_platform_remove(struct platform_device *pdev)
{
	sdtx_device_destroy(platform_get_drvdata(pdev));
	return 0;
}

static const struct acpi_device_id surface_dtx_acpi_match[] = {
	{ "MSHW0133", 0 },
	{ },
};
MODULE_DEVICE_TABLE(acpi, surface_dtx_acpi_match);

static struct platform_driver surface_dtx_platform_driver = {
	.probe = surface_dtx_platform_probe,
	.remove = surface_dtx_platform_remove,
	.driver = {
		.name = "surface_dtx_pltf",
		.acpi_match_table = surface_dtx_acpi_match,
		.pm = &surface_dtx_pm_ops,
		.probe_type = PROBE_PREFER_ASYNCHRONOUS,
	},
};


/* -- SSAM device driver. --------------------------------------------------- */

#ifdef CONFIG_SURFACE_AGGREGATOR_BUS

static int surface_dtx_ssam_probe(struct ssam_device *sdev)
{
	struct sdtx_device *ddev;

	ddev = sdtx_device_create(&sdev->dev, sdev->ctrl);
	if (IS_ERR(ddev))
		return PTR_ERR(ddev);

	ssam_device_set_drvdata(sdev, ddev);
	return 0;
}

static void surface_dtx_ssam_remove(struct ssam_device *sdev)
{
	sdtx_device_destroy(ssam_device_get_drvdata(sdev));
}

static const struct ssam_device_id surface_dtx_ssam_match[] = {
	{ SSAM_SDEV(BAS, 0x01, 0x00, 0x00) },
	{ },
};
MODULE_DEVICE_TABLE(ssam, surface_dtx_ssam_match);

static struct ssam_device_driver surface_dtx_ssam_driver = {
	.probe = surface_dtx_ssam_probe,
	.remove = surface_dtx_ssam_remove,
	.match_table = surface_dtx_ssam_match,
	.driver = {
		.name = "surface_dtx",
		.pm = &surface_dtx_pm_ops,
		.probe_type = PROBE_PREFER_ASYNCHRONOUS,
	},
};

static int ssam_dtx_driver_register(void)
{
	return ssam_device_driver_register(&surface_dtx_ssam_driver);
}

static void ssam_dtx_driver_unregister(void)
{
	ssam_device_driver_unregister(&surface_dtx_ssam_driver);
}

#else /* CONFIG_SURFACE_AGGREGATOR_BUS */

static int ssam_dtx_driver_register(void)
{
	return 0;
}

static void ssam_dtx_driver_unregister(void)
{
}

#endif /* CONFIG_SURFACE_AGGREGATOR_BUS */


/* -- Module setup. --------------------------------------------------------- */

static int __init surface_dtx_init(void)
{
	int status;

	status = ssam_dtx_driver_register();
	if (status)
		return status;

	status = platform_driver_register(&surface_dtx_platform_driver);
	if (status)
		ssam_dtx_driver_unregister();

	return status;
}
module_init(surface_dtx_init);

static void __exit surface_dtx_exit(void)
{
	platform_driver_unregister(&surface_dtx_platform_driver);
	ssam_dtx_driver_unregister();
}
module_exit(surface_dtx_exit);

MODULE_AUTHOR("Maximilian Luz <luzmaximilian@gmail.com>");
MODULE_DESCRIPTION("Detachment-system driver for Surface System Aggregator Module");
MODULE_LICENSE("GPL");
