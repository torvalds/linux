// SPDX-License-Identifier: GPL-2.0+
/*
 * Provides user-space access to the SSAM EC via the /dev/surface/aggregator
 * misc device. Intended for debugging and development.
 *
 * Copyright (C) 2020-2022 Maximilian Luz <luzmaximilian@gmail.com>
 */

#include <linux/fs.h>
#include <linux/ioctl.h>
#include <linux/kernel.h>
#include <linux/kfifo.h>
#include <linux/kref.h>
#include <linux/miscdevice.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/poll.h>
#include <linux/rwsem.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/vmalloc.h>

#include <linux/surface_aggregator/cdev.h>
#include <linux/surface_aggregator/controller.h>
#include <linux/surface_aggregator/serial_hub.h>

#define SSAM_CDEV_DEVICE_NAME	"surface_aggregator_cdev"


/* -- Main structures. ------------------------------------------------------ */

enum ssam_cdev_device_state {
	SSAM_CDEV_DEVICE_SHUTDOWN_BIT = BIT(0),
};

struct ssam_cdev {
	struct kref kref;
	struct rw_semaphore lock;

	struct device *dev;
	struct ssam_controller *ctrl;
	struct miscdevice mdev;
	unsigned long flags;

	struct rw_semaphore client_lock;  /* Guards client list. */
	struct list_head client_list;
};

struct ssam_cdev_client;

struct ssam_cdev_notifier {
	struct ssam_cdev_client *client;
	struct ssam_event_notifier nf;
};

struct ssam_cdev_client {
	struct ssam_cdev *cdev;
	struct list_head node;

	struct mutex notifier_lock;	/* Guards notifier access for registration */
	struct ssam_cdev_notifier *notifier[SSH_NUM_EVENTS];

	struct mutex read_lock;		/* Guards FIFO buffer read access */
	struct mutex write_lock;	/* Guards FIFO buffer write access */
	DECLARE_KFIFO(buffer, u8, 4096);

	wait_queue_head_t waitq;
	struct fasync_struct *fasync;
};

static void __ssam_cdev_release(struct kref *kref)
{
	kfree(container_of(kref, struct ssam_cdev, kref));
}

static struct ssam_cdev *ssam_cdev_get(struct ssam_cdev *cdev)
{
	if (cdev)
		kref_get(&cdev->kref);

	return cdev;
}

static void ssam_cdev_put(struct ssam_cdev *cdev)
{
	if (cdev)
		kref_put(&cdev->kref, __ssam_cdev_release);
}


/* -- Notifier handling. ---------------------------------------------------- */

static u32 ssam_cdev_notifier(struct ssam_event_notifier *nf, const struct ssam_event *in)
{
	struct ssam_cdev_notifier *cdev_nf = container_of(nf, struct ssam_cdev_notifier, nf);
	struct ssam_cdev_client *client = cdev_nf->client;
	struct ssam_cdev_event event;
	size_t n = struct_size(&event, data, in->length);

	/* Translate event. */
	event.target_category = in->target_category;
	event.target_id = in->target_id;
	event.command_id = in->command_id;
	event.instance_id = in->instance_id;
	event.length = in->length;

	mutex_lock(&client->write_lock);

	/* Make sure we have enough space. */
	if (kfifo_avail(&client->buffer) < n) {
		dev_warn(client->cdev->dev,
			 "buffer full, dropping event (tc: %#04x, tid: %#04x, cid: %#04x, iid: %#04x)\n",
			 in->target_category, in->target_id, in->command_id, in->instance_id);
		mutex_unlock(&client->write_lock);
		return 0;
	}

	/* Copy event header and payload. */
	kfifo_in(&client->buffer, (const u8 *)&event, struct_size(&event, data, 0));
	kfifo_in(&client->buffer, &in->data[0], in->length);

	mutex_unlock(&client->write_lock);

	/* Notify waiting readers. */
	kill_fasync(&client->fasync, SIGIO, POLL_IN);
	wake_up_interruptible(&client->waitq);

	/*
	 * Don't mark events as handled, this is the job of a proper driver and
	 * not the debugging interface.
	 */
	return 0;
}

static int ssam_cdev_notifier_register(struct ssam_cdev_client *client, u8 tc, int priority)
{
	const u16 rqid = ssh_tc_to_rqid(tc);
	const u16 event = ssh_rqid_to_event(rqid);
	struct ssam_cdev_notifier *nf;
	int status;

	lockdep_assert_held_read(&client->cdev->lock);

	/* Validate notifier target category. */
	if (!ssh_rqid_is_event(rqid))
		return -EINVAL;

	mutex_lock(&client->notifier_lock);

	/* Check if the notifier has already been registered. */
	if (client->notifier[event]) {
		mutex_unlock(&client->notifier_lock);
		return -EEXIST;
	}

	/* Allocate new notifier. */
	nf = kzalloc(sizeof(*nf), GFP_KERNEL);
	if (!nf) {
		mutex_unlock(&client->notifier_lock);
		return -ENOMEM;
	}

	/*
	 * Create a dummy notifier with the minimal required fields for
	 * observer registration. Note that we can skip fully specifying event
	 * and registry here as we do not need any matching and use silent
	 * registration, which does not enable the corresponding event.
	 */
	nf->client = client;
	nf->nf.base.fn = ssam_cdev_notifier;
	nf->nf.base.priority = priority;
	nf->nf.event.id.target_category = tc;
	nf->nf.event.mask = 0;	/* Do not do any matching. */
	nf->nf.flags = SSAM_EVENT_NOTIFIER_OBSERVER;

	/* Register notifier. */
	status = ssam_notifier_register(client->cdev->ctrl, &nf->nf);
	if (status)
		kfree(nf);
	else
		client->notifier[event] = nf;

	mutex_unlock(&client->notifier_lock);
	return status;
}

static int ssam_cdev_notifier_unregister(struct ssam_cdev_client *client, u8 tc)
{
	const u16 rqid = ssh_tc_to_rqid(tc);
	const u16 event = ssh_rqid_to_event(rqid);
	int status;

	lockdep_assert_held_read(&client->cdev->lock);

	/* Validate notifier target category. */
	if (!ssh_rqid_is_event(rqid))
		return -EINVAL;

	mutex_lock(&client->notifier_lock);

	/* Check if the notifier is currently registered. */
	if (!client->notifier[event]) {
		mutex_unlock(&client->notifier_lock);
		return -ENOENT;
	}

	/* Unregister and free notifier. */
	status = ssam_notifier_unregister(client->cdev->ctrl, &client->notifier[event]->nf);
	kfree(client->notifier[event]);
	client->notifier[event] = NULL;

	mutex_unlock(&client->notifier_lock);
	return status;
}

static void ssam_cdev_notifier_unregister_all(struct ssam_cdev_client *client)
{
	int i;

	down_read(&client->cdev->lock);

	/*
	 * This function may be used during shutdown, thus we need to test for
	 * cdev->ctrl instead of the SSAM_CDEV_DEVICE_SHUTDOWN_BIT bit.
	 */
	if (client->cdev->ctrl) {
		for (i = 0; i < SSH_NUM_EVENTS; i++)
			ssam_cdev_notifier_unregister(client, i + 1);

	} else {
		int count = 0;

		/*
		 * Device has been shut down. Any notifier remaining is a bug,
		 * so warn about that as this would otherwise hardly be
		 * noticeable. Nevertheless, free them as well.
		 */
		mutex_lock(&client->notifier_lock);
		for (i = 0; i < SSH_NUM_EVENTS; i++) {
			count += !!(client->notifier[i]);
			kfree(client->notifier[i]);
			client->notifier[i] = NULL;
		}
		mutex_unlock(&client->notifier_lock);

		WARN_ON(count > 0);
	}

	up_read(&client->cdev->lock);
}


/* -- IOCTL functions. ------------------------------------------------------ */

static long ssam_cdev_request(struct ssam_cdev_client *client, struct ssam_cdev_request __user *r)
{
	struct ssam_cdev_request rqst;
	struct ssam_request spec = {};
	struct ssam_response rsp = {};
	const void __user *plddata;
	void __user *rspdata;
	int status = 0, ret = 0, tmp;

	lockdep_assert_held_read(&client->cdev->lock);

	ret = copy_struct_from_user(&rqst, sizeof(rqst), r, sizeof(*r));
	if (ret)
		goto out;

	plddata = u64_to_user_ptr(rqst.payload.data);
	rspdata = u64_to_user_ptr(rqst.response.data);

	/* Setup basic request fields. */
	spec.target_category = rqst.target_category;
	spec.target_id = rqst.target_id;
	spec.command_id = rqst.command_id;
	spec.instance_id = rqst.instance_id;
	spec.flags = 0;
	spec.length = rqst.payload.length;
	spec.payload = NULL;

	if (rqst.flags & SSAM_CDEV_REQUEST_HAS_RESPONSE)
		spec.flags |= SSAM_REQUEST_HAS_RESPONSE;

	if (rqst.flags & SSAM_CDEV_REQUEST_UNSEQUENCED)
		spec.flags |= SSAM_REQUEST_UNSEQUENCED;

	rsp.capacity = rqst.response.length;
	rsp.length = 0;
	rsp.pointer = NULL;

	/* Get request payload from user-space. */
	if (spec.length) {
		if (!plddata) {
			ret = -EINVAL;
			goto out;
		}

		/*
		 * Note: spec.length is limited to U16_MAX bytes via struct
		 * ssam_cdev_request. This is slightly larger than the
		 * theoretical maximum (SSH_COMMAND_MAX_PAYLOAD_SIZE) of the
		 * underlying protocol (note that nothing remotely this size
		 * should ever be allocated in any normal case). This size is
		 * validated later in ssam_request_sync(), for allocation the
		 * bound imposed by u16 should be enough.
		 */
		spec.payload = kzalloc(spec.length, GFP_KERNEL);
		if (!spec.payload) {
			ret = -ENOMEM;
			goto out;
		}

		if (copy_from_user((void *)spec.payload, plddata, spec.length)) {
			ret = -EFAULT;
			goto out;
		}
	}

	/* Allocate response buffer. */
	if (rsp.capacity) {
		if (!rspdata) {
			ret = -EINVAL;
			goto out;
		}

		/*
		 * Note: rsp.capacity is limited to U16_MAX bytes via struct
		 * ssam_cdev_request. This is slightly larger than the
		 * theoretical maximum (SSH_COMMAND_MAX_PAYLOAD_SIZE) of the
		 * underlying protocol (note that nothing remotely this size
		 * should ever be allocated in any normal case). In later use,
		 * this capacity does not have to be strictly bounded, as it
		 * is only used as an output buffer to be written to. For
		 * allocation the bound imposed by u16 should be enough.
		 */
		rsp.pointer = kzalloc(rsp.capacity, GFP_KERNEL);
		if (!rsp.pointer) {
			ret = -ENOMEM;
			goto out;
		}
	}

	/* Perform request. */
	status = ssam_request_sync(client->cdev->ctrl, &spec, &rsp);
	if (status)
		goto out;

	/* Copy response to user-space. */
	if (rsp.length && copy_to_user(rspdata, rsp.pointer, rsp.length))
		ret = -EFAULT;

out:
	/* Always try to set response-length and status. */
	tmp = put_user(rsp.length, &r->response.length);
	if (tmp)
		ret = tmp;

	tmp = put_user(status, &r->status);
	if (tmp)
		ret = tmp;

	/* Cleanup. */
	kfree(spec.payload);
	kfree(rsp.pointer);

	return ret;
}

static long ssam_cdev_notif_register(struct ssam_cdev_client *client,
				     const struct ssam_cdev_notifier_desc __user *d)
{
	struct ssam_cdev_notifier_desc desc;
	long ret;

	lockdep_assert_held_read(&client->cdev->lock);

	ret = copy_struct_from_user(&desc, sizeof(desc), d, sizeof(*d));
	if (ret)
		return ret;

	return ssam_cdev_notifier_register(client, desc.target_category, desc.priority);
}

static long ssam_cdev_notif_unregister(struct ssam_cdev_client *client,
				       const struct ssam_cdev_notifier_desc __user *d)
{
	struct ssam_cdev_notifier_desc desc;
	long ret;

	lockdep_assert_held_read(&client->cdev->lock);

	ret = copy_struct_from_user(&desc, sizeof(desc), d, sizeof(*d));
	if (ret)
		return ret;

	return ssam_cdev_notifier_unregister(client, desc.target_category);
}

static long ssam_cdev_event_enable(struct ssam_cdev_client *client,
				   const struct ssam_cdev_event_desc __user *d)
{
	struct ssam_cdev_event_desc desc;
	struct ssam_event_registry reg;
	struct ssam_event_id id;
	long ret;

	lockdep_assert_held_read(&client->cdev->lock);

	/* Read descriptor from user-space. */
	ret = copy_struct_from_user(&desc, sizeof(desc), d, sizeof(*d));
	if (ret)
		return ret;

	/* Translate descriptor. */
	reg.target_category = desc.reg.target_category;
	reg.target_id = desc.reg.target_id;
	reg.cid_enable = desc.reg.cid_enable;
	reg.cid_disable = desc.reg.cid_disable;

	id.target_category = desc.id.target_category;
	id.instance = desc.id.instance;

	/* Disable event. */
	return ssam_controller_event_enable(client->cdev->ctrl, reg, id, desc.flags);
}

static long ssam_cdev_event_disable(struct ssam_cdev_client *client,
				    const struct ssam_cdev_event_desc __user *d)
{
	struct ssam_cdev_event_desc desc;
	struct ssam_event_registry reg;
	struct ssam_event_id id;
	long ret;

	lockdep_assert_held_read(&client->cdev->lock);

	/* Read descriptor from user-space. */
	ret = copy_struct_from_user(&desc, sizeof(desc), d, sizeof(*d));
	if (ret)
		return ret;

	/* Translate descriptor. */
	reg.target_category = desc.reg.target_category;
	reg.target_id = desc.reg.target_id;
	reg.cid_enable = desc.reg.cid_enable;
	reg.cid_disable = desc.reg.cid_disable;

	id.target_category = desc.id.target_category;
	id.instance = desc.id.instance;

	/* Disable event. */
	return ssam_controller_event_disable(client->cdev->ctrl, reg, id, desc.flags);
}


/* -- File operations. ------------------------------------------------------ */

static int ssam_cdev_device_open(struct inode *inode, struct file *filp)
{
	struct miscdevice *mdev = filp->private_data;
	struct ssam_cdev_client *client;
	struct ssam_cdev *cdev = container_of(mdev, struct ssam_cdev, mdev);

	/* Initialize client */
	client = vzalloc(sizeof(*client));
	if (!client)
		return -ENOMEM;

	client->cdev = ssam_cdev_get(cdev);

	INIT_LIST_HEAD(&client->node);

	mutex_init(&client->notifier_lock);

	mutex_init(&client->read_lock);
	mutex_init(&client->write_lock);
	INIT_KFIFO(client->buffer);
	init_waitqueue_head(&client->waitq);

	filp->private_data = client;

	/* Attach client. */
	down_write(&cdev->client_lock);

	if (test_bit(SSAM_CDEV_DEVICE_SHUTDOWN_BIT, &cdev->flags)) {
		up_write(&cdev->client_lock);
		mutex_destroy(&client->write_lock);
		mutex_destroy(&client->read_lock);
		mutex_destroy(&client->notifier_lock);
		ssam_cdev_put(client->cdev);
		vfree(client);
		return -ENODEV;
	}
	list_add_tail(&client->node, &cdev->client_list);

	up_write(&cdev->client_lock);

	stream_open(inode, filp);
	return 0;
}

static int ssam_cdev_device_release(struct inode *inode, struct file *filp)
{
	struct ssam_cdev_client *client = filp->private_data;

	/* Force-unregister all remaining notifiers of this client. */
	ssam_cdev_notifier_unregister_all(client);

	/* Detach client. */
	down_write(&client->cdev->client_lock);
	list_del(&client->node);
	up_write(&client->cdev->client_lock);

	/* Free client. */
	mutex_destroy(&client->write_lock);
	mutex_destroy(&client->read_lock);

	mutex_destroy(&client->notifier_lock);

	ssam_cdev_put(client->cdev);
	vfree(client);

	return 0;
}

static long __ssam_cdev_device_ioctl(struct ssam_cdev_client *client, unsigned int cmd,
				     unsigned long arg)
{
	lockdep_assert_held_read(&client->cdev->lock);

	switch (cmd) {
	case SSAM_CDEV_REQUEST:
		return ssam_cdev_request(client, (struct ssam_cdev_request __user *)arg);

	case SSAM_CDEV_NOTIF_REGISTER:
		return ssam_cdev_notif_register(client,
						(struct ssam_cdev_notifier_desc __user *)arg);

	case SSAM_CDEV_NOTIF_UNREGISTER:
		return ssam_cdev_notif_unregister(client,
						  (struct ssam_cdev_notifier_desc __user *)arg);

	case SSAM_CDEV_EVENT_ENABLE:
		return ssam_cdev_event_enable(client, (struct ssam_cdev_event_desc __user *)arg);

	case SSAM_CDEV_EVENT_DISABLE:
		return ssam_cdev_event_disable(client, (struct ssam_cdev_event_desc __user *)arg);

	default:
		return -ENOTTY;
	}
}

static long ssam_cdev_device_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	struct ssam_cdev_client *client = file->private_data;
	long status;

	/* Ensure that controller is valid for as long as we need it. */
	if (down_read_killable(&client->cdev->lock))
		return -ERESTARTSYS;

	if (test_bit(SSAM_CDEV_DEVICE_SHUTDOWN_BIT, &client->cdev->flags)) {
		up_read(&client->cdev->lock);
		return -ENODEV;
	}

	status = __ssam_cdev_device_ioctl(client, cmd, arg);

	up_read(&client->cdev->lock);
	return status;
}

static ssize_t ssam_cdev_read(struct file *file, char __user *buf, size_t count, loff_t *offs)
{
	struct ssam_cdev_client *client = file->private_data;
	struct ssam_cdev *cdev = client->cdev;
	unsigned int copied;
	int status = 0;

	if (down_read_killable(&cdev->lock))
		return -ERESTARTSYS;

	/* Make sure we're not shut down. */
	if (test_bit(SSAM_CDEV_DEVICE_SHUTDOWN_BIT, &cdev->flags)) {
		up_read(&cdev->lock);
		return -ENODEV;
	}

	do {
		/* Check availability, wait if necessary. */
		if (kfifo_is_empty(&client->buffer)) {
			up_read(&cdev->lock);

			if (file->f_flags & O_NONBLOCK)
				return -EAGAIN;

			status = wait_event_interruptible(client->waitq,
							  !kfifo_is_empty(&client->buffer) ||
							  test_bit(SSAM_CDEV_DEVICE_SHUTDOWN_BIT,
								   &cdev->flags));
			if (status < 0)
				return status;

			if (down_read_killable(&cdev->lock))
				return -ERESTARTSYS;

			/* Need to check that we're not shut down again. */
			if (test_bit(SSAM_CDEV_DEVICE_SHUTDOWN_BIT, &cdev->flags)) {
				up_read(&cdev->lock);
				return -ENODEV;
			}
		}

		/* Try to read from FIFO. */
		if (mutex_lock_interruptible(&client->read_lock)) {
			up_read(&cdev->lock);
			return -ERESTARTSYS;
		}

		status = kfifo_to_user(&client->buffer, buf, count, &copied);
		mutex_unlock(&client->read_lock);

		if (status < 0) {
			up_read(&cdev->lock);
			return status;
		}

		/* We might not have gotten anything, check this here. */
		if (copied == 0 && (file->f_flags & O_NONBLOCK)) {
			up_read(&cdev->lock);
			return -EAGAIN;
		}
	} while (copied == 0);

	up_read(&cdev->lock);
	return copied;
}

static __poll_t ssam_cdev_poll(struct file *file, struct poll_table_struct *pt)
{
	struct ssam_cdev_client *client = file->private_data;
	__poll_t events = 0;

	if (test_bit(SSAM_CDEV_DEVICE_SHUTDOWN_BIT, &client->cdev->flags))
		return EPOLLHUP | EPOLLERR;

	poll_wait(file, &client->waitq, pt);

	if (!kfifo_is_empty(&client->buffer))
		events |= EPOLLIN | EPOLLRDNORM;

	return events;
}

static int ssam_cdev_fasync(int fd, struct file *file, int on)
{
	struct ssam_cdev_client *client = file->private_data;

	return fasync_helper(fd, file, on, &client->fasync);
}

static const struct file_operations ssam_controller_fops = {
	.owner          = THIS_MODULE,
	.open           = ssam_cdev_device_open,
	.release        = ssam_cdev_device_release,
	.read           = ssam_cdev_read,
	.poll           = ssam_cdev_poll,
	.fasync         = ssam_cdev_fasync,
	.unlocked_ioctl = ssam_cdev_device_ioctl,
	.compat_ioctl   = ssam_cdev_device_ioctl,
	.llseek         = no_llseek,
};


/* -- Device and driver setup ----------------------------------------------- */

static int ssam_dbg_device_probe(struct platform_device *pdev)
{
	struct ssam_controller *ctrl;
	struct ssam_cdev *cdev;
	int status;

	ctrl = ssam_client_bind(&pdev->dev);
	if (IS_ERR(ctrl))
		return PTR_ERR(ctrl) == -ENODEV ? -EPROBE_DEFER : PTR_ERR(ctrl);

	cdev = kzalloc(sizeof(*cdev), GFP_KERNEL);
	if (!cdev)
		return -ENOMEM;

	kref_init(&cdev->kref);
	init_rwsem(&cdev->lock);
	cdev->ctrl = ctrl;
	cdev->dev = &pdev->dev;

	cdev->mdev.parent   = &pdev->dev;
	cdev->mdev.minor    = MISC_DYNAMIC_MINOR;
	cdev->mdev.name     = "surface_aggregator";
	cdev->mdev.nodename = "surface/aggregator";
	cdev->mdev.fops     = &ssam_controller_fops;

	init_rwsem(&cdev->client_lock);
	INIT_LIST_HEAD(&cdev->client_list);

	status = misc_register(&cdev->mdev);
	if (status) {
		kfree(cdev);
		return status;
	}

	platform_set_drvdata(pdev, cdev);
	return 0;
}

static int ssam_dbg_device_remove(struct platform_device *pdev)
{
	struct ssam_cdev *cdev = platform_get_drvdata(pdev);
	struct ssam_cdev_client *client;

	/*
	 * Mark device as shut-down. Prevent new clients from being added and
	 * new operations from being executed.
	 */
	set_bit(SSAM_CDEV_DEVICE_SHUTDOWN_BIT, &cdev->flags);

	down_write(&cdev->client_lock);

	/* Remove all notifiers registered by us. */
	list_for_each_entry(client, &cdev->client_list, node) {
		ssam_cdev_notifier_unregister_all(client);
	}

	/* Wake up async clients. */
	list_for_each_entry(client, &cdev->client_list, node) {
		kill_fasync(&client->fasync, SIGIO, POLL_HUP);
	}

	/* Wake up blocking clients. */
	list_for_each_entry(client, &cdev->client_list, node) {
		wake_up_interruptible(&client->waitq);
	}

	up_write(&cdev->client_lock);

	/*
	 * The controller is only guaranteed to be valid for as long as the
	 * driver is bound. Remove controller so that any lingering open files
	 * cannot access it any more after we're gone.
	 */
	down_write(&cdev->lock);
	cdev->ctrl = NULL;
	cdev->dev = NULL;
	up_write(&cdev->lock);

	misc_deregister(&cdev->mdev);

	ssam_cdev_put(cdev);
	return 0;
}

static struct platform_device *ssam_cdev_device;

static struct platform_driver ssam_cdev_driver = {
	.probe = ssam_dbg_device_probe,
	.remove = ssam_dbg_device_remove,
	.driver = {
		.name = SSAM_CDEV_DEVICE_NAME,
		.probe_type = PROBE_PREFER_ASYNCHRONOUS,
	},
};

static int __init ssam_debug_init(void)
{
	int status;

	ssam_cdev_device = platform_device_alloc(SSAM_CDEV_DEVICE_NAME,
						 PLATFORM_DEVID_NONE);
	if (!ssam_cdev_device)
		return -ENOMEM;

	status = platform_device_add(ssam_cdev_device);
	if (status)
		goto err_device;

	status = platform_driver_register(&ssam_cdev_driver);
	if (status)
		goto err_driver;

	return 0;

err_driver:
	platform_device_del(ssam_cdev_device);
err_device:
	platform_device_put(ssam_cdev_device);
	return status;
}
module_init(ssam_debug_init);

static void __exit ssam_debug_exit(void)
{
	platform_driver_unregister(&ssam_cdev_driver);
	platform_device_unregister(ssam_cdev_device);
}
module_exit(ssam_debug_exit);

MODULE_AUTHOR("Maximilian Luz <luzmaximilian@gmail.com>");
MODULE_DESCRIPTION("User-space interface for Surface System Aggregator Module");
MODULE_LICENSE("GPL");
