// SPDX-License-Identifier: GPL-2.0
/*
 * Miscellaneous character driver for ChromeOS Embedded Controller
 *
 * Copyright 2014 Google, Inc.
 * Copyright 2019 Google LLC
 *
 * This file is a rework and part of the code is ported from
 * drivers/mfd/cros_ec_dev.c that was originally written by
 * Bill Richardson.
 */

#include <linux/init.h>
#include <linux/device.h>
#include <linux/fs.h>
#include <linux/miscdevice.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/notifier.h>
#include <linux/platform_data/cros_ec_chardev.h>
#include <linux/platform_data/cros_ec_commands.h>
#include <linux/platform_data/cros_ec_proto.h>
#include <linux/platform_device.h>
#include <linux/poll.h>
#include <linux/slab.h>
#include <linux/types.h>
#include <linux/uaccess.h>

#define DRV_NAME		"cros-ec-chardev"

/* Arbitrary bounded size for the event queue */
#define CROS_MAX_EVENT_LEN	PAGE_SIZE

struct chardev_data {
	struct cros_ec_dev *ec_dev;
	struct miscdevice misc;
};

struct chardev_priv {
	struct cros_ec_dev *ec_dev;
	struct notifier_block notifier;
	wait_queue_head_t wait_event;
	unsigned long event_mask;
	struct list_head events;
	size_t event_len;
};

struct ec_event {
	struct list_head node;
	size_t size;
	u8 event_type;
	u8 data[];
};

static int ec_get_version(struct cros_ec_dev *ec, char *str, int maxlen)
{
	static const char * const current_image_name[] = {
		"unknown", "read-only", "read-write", "invalid",
	};
	struct ec_response_get_version *resp;
	struct cros_ec_command *msg;
	int ret;

	msg = kzalloc(sizeof(*msg) + sizeof(*resp), GFP_KERNEL);
	if (!msg)
		return -ENOMEM;

	msg->command = EC_CMD_GET_VERSION + ec->cmd_offset;
	msg->insize = sizeof(*resp);

	ret = cros_ec_cmd_xfer_status(ec->ec_dev, msg);
	if (ret < 0) {
		snprintf(str, maxlen,
			 "Unknown EC version, returned error: %d\n",
			 msg->result);
		goto exit;
	}

	resp = (struct ec_response_get_version *)msg->data;
	if (resp->current_image >= ARRAY_SIZE(current_image_name))
		resp->current_image = 3; /* invalid */

	snprintf(str, maxlen, "%s\n%s\n%s\n%s\n", CROS_EC_DEV_VERSION,
		 resp->version_string_ro, resp->version_string_rw,
		 current_image_name[resp->current_image]);

	ret = 0;
exit:
	kfree(msg);
	return ret;
}

static int cros_ec_chardev_mkbp_event(struct notifier_block *nb,
				      unsigned long queued_during_suspend,
				      void *_notify)
{
	struct chardev_priv *priv = container_of(nb, struct chardev_priv,
						 notifier);
	struct cros_ec_device *ec_dev = priv->ec_dev->ec_dev;
	struct ec_event *event;
	unsigned long event_bit = 1 << ec_dev->event_data.event_type;
	int total_size = sizeof(*event) + ec_dev->event_size;

	if (!(event_bit & priv->event_mask) ||
	    (priv->event_len + total_size) > CROS_MAX_EVENT_LEN)
		return NOTIFY_DONE;

	event = kzalloc(total_size, GFP_KERNEL);
	if (!event)
		return NOTIFY_DONE;

	event->size = ec_dev->event_size;
	event->event_type = ec_dev->event_data.event_type;
	memcpy(event->data, &ec_dev->event_data.data, ec_dev->event_size);

	spin_lock(&priv->wait_event.lock);
	list_add_tail(&event->node, &priv->events);
	priv->event_len += total_size;
	wake_up_locked(&priv->wait_event);
	spin_unlock(&priv->wait_event.lock);

	return NOTIFY_OK;
}

static struct ec_event *cros_ec_chardev_fetch_event(struct chardev_priv *priv,
						    bool fetch, bool block)
{
	struct ec_event *event;
	int err;

	spin_lock(&priv->wait_event.lock);
	if (!block && list_empty(&priv->events)) {
		event = ERR_PTR(-EWOULDBLOCK);
		goto out;
	}

	if (!fetch) {
		event = NULL;
		goto out;
	}

	err = wait_event_interruptible_locked(priv->wait_event,
					      !list_empty(&priv->events));
	if (err) {
		event = ERR_PTR(err);
		goto out;
	}

	event = list_first_entry(&priv->events, struct ec_event, node);
	list_del(&event->node);
	priv->event_len -= sizeof(*event) + event->size;

out:
	spin_unlock(&priv->wait_event.lock);
	return event;
}

/*
 * Device file ops
 */
static int cros_ec_chardev_open(struct inode *inode, struct file *filp)
{
	struct miscdevice *mdev = filp->private_data;
	struct cros_ec_dev *ec_dev = dev_get_drvdata(mdev->parent);
	struct chardev_priv *priv;
	int ret;

	priv = kzalloc(sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->ec_dev = ec_dev;
	filp->private_data = priv;
	INIT_LIST_HEAD(&priv->events);
	init_waitqueue_head(&priv->wait_event);
	nonseekable_open(inode, filp);

	priv->notifier.notifier_call = cros_ec_chardev_mkbp_event;
	ret = blocking_notifier_chain_register(&ec_dev->ec_dev->event_notifier,
					       &priv->notifier);
	if (ret) {
		dev_err(ec_dev->dev, "failed to register event notifier\n");
		kfree(priv);
	}

	return ret;
}

static __poll_t cros_ec_chardev_poll(struct file *filp, poll_table *wait)
{
	struct chardev_priv *priv = filp->private_data;

	poll_wait(filp, &priv->wait_event, wait);

	if (list_empty(&priv->events))
		return 0;

	return EPOLLIN | EPOLLRDNORM;
}

static ssize_t cros_ec_chardev_read(struct file *filp, char __user *buffer,
				     size_t length, loff_t *offset)
{
	char msg[sizeof(struct ec_response_get_version) +
		 sizeof(CROS_EC_DEV_VERSION)];
	struct chardev_priv *priv = filp->private_data;
	struct cros_ec_dev *ec_dev = priv->ec_dev;
	size_t count;
	int ret;

	if (priv->event_mask) { /* queued MKBP event */
		struct ec_event *event;

		event = cros_ec_chardev_fetch_event(priv, length != 0,
						!(filp->f_flags & O_NONBLOCK));
		if (IS_ERR(event))
			return PTR_ERR(event);
		/*
		 * length == 0 is special - no IO is done but we check
		 * for error conditions.
		 */
		if (length == 0)
			return 0;

		/* The event is 1 byte of type plus the payload */
		count = min(length, event->size + 1);
		ret = copy_to_user(buffer, &event->event_type, count);
		kfree(event);
		if (ret) /* the copy failed */
			return -EFAULT;
		*offset = count;
		return count;
	}

	/*
	 * Legacy behavior if no event mask is defined
	 */
	if (*offset != 0)
		return 0;

	ret = ec_get_version(ec_dev, msg, sizeof(msg));
	if (ret)
		return ret;

	count = min(length, strlen(msg));

	if (copy_to_user(buffer, msg, count))
		return -EFAULT;

	*offset = count;
	return count;
}

static int cros_ec_chardev_release(struct inode *inode, struct file *filp)
{
	struct chardev_priv *priv = filp->private_data;
	struct cros_ec_dev *ec_dev = priv->ec_dev;
	struct ec_event *event, *e;

	blocking_notifier_chain_unregister(&ec_dev->ec_dev->event_notifier,
					   &priv->notifier);

	list_for_each_entry_safe(event, e, &priv->events, node) {
		list_del(&event->node);
		kfree(event);
	}
	kfree(priv);

	return 0;
}

/*
 * Ioctls
 */
static long cros_ec_chardev_ioctl_xcmd(struct cros_ec_dev *ec, void __user *arg)
{
	struct cros_ec_command *s_cmd;
	struct cros_ec_command u_cmd;
	long ret;

	if (copy_from_user(&u_cmd, arg, sizeof(u_cmd)))
		return -EFAULT;

	if (u_cmd.outsize > EC_MAX_MSG_BYTES ||
	    u_cmd.insize > EC_MAX_MSG_BYTES)
		return -EINVAL;

	s_cmd = kzalloc(sizeof(*s_cmd) + max(u_cmd.outsize, u_cmd.insize),
			GFP_KERNEL);
	if (!s_cmd)
		return -ENOMEM;

	if (copy_from_user(s_cmd, arg, sizeof(*s_cmd) + u_cmd.outsize)) {
		ret = -EFAULT;
		goto exit;
	}

	if (u_cmd.outsize != s_cmd->outsize ||
	    u_cmd.insize != s_cmd->insize) {
		ret = -EINVAL;
		goto exit;
	}

	s_cmd->command += ec->cmd_offset;
	ret = cros_ec_cmd_xfer(ec->ec_dev, s_cmd);
	/* Only copy data to userland if data was received. */
	if (ret < 0)
		goto exit;

	if (copy_to_user(arg, s_cmd, sizeof(*s_cmd) + s_cmd->insize))
		ret = -EFAULT;
exit:
	kfree(s_cmd);
	return ret;
}

static long cros_ec_chardev_ioctl_readmem(struct cros_ec_dev *ec,
					   void __user *arg)
{
	struct cros_ec_device *ec_dev = ec->ec_dev;
	struct cros_ec_readmem s_mem = { };
	long num;

	/* Not every platform supports direct reads */
	if (!ec_dev->cmd_readmem)
		return -ENOTTY;

	if (copy_from_user(&s_mem, arg, sizeof(s_mem)))
		return -EFAULT;

	if (s_mem.bytes > sizeof(s_mem.buffer))
		return -EINVAL;

	num = ec_dev->cmd_readmem(ec_dev, s_mem.offset, s_mem.bytes,
				  s_mem.buffer);
	if (num <= 0)
		return num;

	if (copy_to_user((void __user *)arg, &s_mem, sizeof(s_mem)))
		return -EFAULT;

	return num;
}

static long cros_ec_chardev_ioctl(struct file *filp, unsigned int cmd,
				   unsigned long arg)
{
	struct chardev_priv *priv = filp->private_data;
	struct cros_ec_dev *ec = priv->ec_dev;

	if (_IOC_TYPE(cmd) != CROS_EC_DEV_IOC)
		return -ENOTTY;

	switch (cmd) {
	case CROS_EC_DEV_IOCXCMD:
		return cros_ec_chardev_ioctl_xcmd(ec, (void __user *)arg);
	case CROS_EC_DEV_IOCRDMEM:
		return cros_ec_chardev_ioctl_readmem(ec, (void __user *)arg);
	case CROS_EC_DEV_IOCEVENTMASK:
		priv->event_mask = arg;
		return 0;
	}

	return -ENOTTY;
}

static const struct file_operations chardev_fops = {
	.open		= cros_ec_chardev_open,
	.poll		= cros_ec_chardev_poll,
	.read		= cros_ec_chardev_read,
	.release	= cros_ec_chardev_release,
	.unlocked_ioctl	= cros_ec_chardev_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl	= cros_ec_chardev_ioctl,
#endif
};

static int cros_ec_chardev_probe(struct platform_device *pdev)
{
	struct cros_ec_dev *ec_dev = dev_get_drvdata(pdev->dev.parent);
	struct cros_ec_platform *ec_platform = dev_get_platdata(ec_dev->dev);
	struct chardev_data *data;

	/* Create a char device: we want to create it anew */
	data = devm_kzalloc(&pdev->dev, sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	data->ec_dev = ec_dev;
	data->misc.minor = MISC_DYNAMIC_MINOR;
	data->misc.fops = &chardev_fops;
	data->misc.name = ec_platform->ec_name;
	data->misc.parent = pdev->dev.parent;

	dev_set_drvdata(&pdev->dev, data);

	return misc_register(&data->misc);
}

static void cros_ec_chardev_remove(struct platform_device *pdev)
{
	struct chardev_data *data = dev_get_drvdata(&pdev->dev);

	misc_deregister(&data->misc);
}

static const struct platform_device_id cros_ec_chardev_id[] = {
	{ DRV_NAME, 0 },
	{}
};
MODULE_DEVICE_TABLE(platform, cros_ec_chardev_id);

static struct platform_driver cros_ec_chardev_driver = {
	.driver = {
		.name = DRV_NAME,
	},
	.probe = cros_ec_chardev_probe,
	.remove_new = cros_ec_chardev_remove,
	.id_table = cros_ec_chardev_id,
};

module_platform_driver(cros_ec_chardev_driver);

MODULE_AUTHOR("Enric Balletbo i Serra <enric.balletbo@collabora.com>");
MODULE_DESCRIPTION("ChromeOS EC Miscellaneous Character Driver");
MODULE_LICENSE("GPL");
