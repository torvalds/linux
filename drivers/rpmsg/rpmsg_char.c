// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2022, STMicroelectronics
 * Copyright (c) 2016, Linaro Ltd.
 * Copyright (c) 2012, Michal Simek <monstr@monstr.eu>
 * Copyright (c) 2012, PetaLogix
 * Copyright (c) 2011, Texas Instruments, Inc.
 * Copyright (c) 2011, Google, Inc.
 *
 * Based on rpmsg performance statistics driver by Michal Simek, which in turn
 * was based on TI & Google OMX rpmsg driver.
 */

#define pr_fmt(fmt)	KBUILD_MODNAME ": " fmt

#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/fs.h>
#include <linux/idr.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/poll.h>
#include <linux/rpmsg.h>
#include <linux/skbuff.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <uapi/linux/rpmsg.h>

#include "rpmsg_char.h"
#include "rpmsg_internal.h"

#define RPMSG_DEV_MAX	(MINORMASK + 1)

static dev_t rpmsg_major;

static DEFINE_IDA(rpmsg_ept_ida);
static DEFINE_IDA(rpmsg_minor_ida);

#define dev_to_eptdev(dev) container_of(dev, struct rpmsg_eptdev, dev)
#define cdev_to_eptdev(i_cdev) container_of(i_cdev, struct rpmsg_eptdev, cdev)

/**
 * struct rpmsg_eptdev - endpoint device context
 * @dev:	endpoint device
 * @cdev:	cdev for the endpoint device
 * @rpdev:	underlaying rpmsg device
 * @chinfo:	info used to open the endpoint
 * @ept_lock:	synchronization of @ept modifications
 * @ept:	rpmsg endpoint reference, when open
 * @queue_lock:	synchronization of @queue operations
 * @queue:	incoming message queue
 * @readq:	wait object for incoming queue
 * @default_ept: set to channel default endpoint if the default endpoint should be re-used
 *              on device open to prevent endpoint address update.
 * @remote_flow_restricted: to indicate if the remote has requested for flow to be limited
 * @remote_flow_updated: to indicate if the flow control has been requested
 */
struct rpmsg_eptdev {
	struct device dev;
	struct cdev cdev;

	struct rpmsg_device *rpdev;
	struct rpmsg_channel_info chinfo;

	struct mutex ept_lock;
	struct rpmsg_endpoint *ept;
	struct rpmsg_endpoint *default_ept;

	spinlock_t queue_lock;
	struct sk_buff_head queue;
	wait_queue_head_t readq;

	bool remote_flow_restricted;
	bool remote_flow_updated;
};

int rpmsg_chrdev_eptdev_destroy(struct device *dev, void *data)
{
	struct rpmsg_eptdev *eptdev = dev_to_eptdev(dev);

	mutex_lock(&eptdev->ept_lock);
	eptdev->rpdev = NULL;
	if (eptdev->ept) {
		/* The default endpoint is released by the rpmsg core */
		if (!eptdev->default_ept)
			rpmsg_destroy_ept(eptdev->ept);
		eptdev->ept = NULL;
	}
	mutex_unlock(&eptdev->ept_lock);

	/* wake up any blocked readers */
	wake_up_interruptible(&eptdev->readq);

	cdev_device_del(&eptdev->cdev, &eptdev->dev);
	put_device(&eptdev->dev);

	return 0;
}
EXPORT_SYMBOL(rpmsg_chrdev_eptdev_destroy);

static int rpmsg_ept_cb(struct rpmsg_device *rpdev, void *buf, int len,
			void *priv, u32 addr)
{
	struct rpmsg_eptdev *eptdev = priv;
	struct sk_buff *skb;

	skb = alloc_skb(len, GFP_ATOMIC);
	if (!skb)
		return -ENOMEM;

	skb_put_data(skb, buf, len);

	spin_lock(&eptdev->queue_lock);
	skb_queue_tail(&eptdev->queue, skb);
	spin_unlock(&eptdev->queue_lock);

	/* wake up any blocking processes, waiting for new data */
	wake_up_interruptible(&eptdev->readq);

	return 0;
}

static int rpmsg_ept_flow_cb(struct rpmsg_device *rpdev, void *priv, bool enable)
{
	struct rpmsg_eptdev *eptdev = priv;

	eptdev->remote_flow_restricted = enable;
	eptdev->remote_flow_updated = true;

	wake_up_interruptible(&eptdev->readq);

	return 0;
}

static int rpmsg_eptdev_open(struct inode *inode, struct file *filp)
{
	struct rpmsg_eptdev *eptdev = cdev_to_eptdev(inode->i_cdev);
	struct rpmsg_endpoint *ept;
	struct rpmsg_device *rpdev = eptdev->rpdev;
	struct device *dev = &eptdev->dev;

	mutex_lock(&eptdev->ept_lock);
	if (eptdev->ept) {
		mutex_unlock(&eptdev->ept_lock);
		return -EBUSY;
	}

	if (!eptdev->rpdev) {
		mutex_unlock(&eptdev->ept_lock);
		return -ENETRESET;
	}

	get_device(dev);

	/*
	 * If the default_ept is set, the rpmsg device default endpoint is used.
	 * Else a new endpoint is created on open that will be destroyed on release.
	 */
	if (eptdev->default_ept)
		ept = eptdev->default_ept;
	else
		ept = rpmsg_create_ept(rpdev, rpmsg_ept_cb, eptdev, eptdev->chinfo);

	if (!ept) {
		dev_err(dev, "failed to open %s\n", eptdev->chinfo.name);
		put_device(dev);
		mutex_unlock(&eptdev->ept_lock);
		return -EINVAL;
	}

	ept->flow_cb = rpmsg_ept_flow_cb;
	eptdev->ept = ept;
	filp->private_data = eptdev;
	mutex_unlock(&eptdev->ept_lock);

	return 0;
}

static int rpmsg_eptdev_release(struct inode *inode, struct file *filp)
{
	struct rpmsg_eptdev *eptdev = cdev_to_eptdev(inode->i_cdev);
	struct device *dev = &eptdev->dev;

	/* Close the endpoint, if it's not already destroyed by the parent */
	mutex_lock(&eptdev->ept_lock);
	if (eptdev->ept) {
		if (!eptdev->default_ept)
			rpmsg_destroy_ept(eptdev->ept);
		eptdev->ept = NULL;
	}
	mutex_unlock(&eptdev->ept_lock);
	eptdev->remote_flow_updated = false;

	/* Discard all SKBs */
	skb_queue_purge(&eptdev->queue);

	put_device(dev);

	return 0;
}

static ssize_t rpmsg_eptdev_read_iter(struct kiocb *iocb, struct iov_iter *to)
{
	struct file *filp = iocb->ki_filp;
	struct rpmsg_eptdev *eptdev = filp->private_data;
	unsigned long flags;
	struct sk_buff *skb;
	int use;

	if (!eptdev->ept)
		return -EPIPE;

	spin_lock_irqsave(&eptdev->queue_lock, flags);

	/* Wait for data in the queue */
	if (skb_queue_empty(&eptdev->queue)) {
		spin_unlock_irqrestore(&eptdev->queue_lock, flags);

		if (filp->f_flags & O_NONBLOCK)
			return -EAGAIN;

		/* Wait until we get data or the endpoint goes away */
		if (wait_event_interruptible(eptdev->readq,
					     !skb_queue_empty(&eptdev->queue) ||
					     !eptdev->ept))
			return -ERESTARTSYS;

		/* We lost the endpoint while waiting */
		if (!eptdev->ept)
			return -EPIPE;

		spin_lock_irqsave(&eptdev->queue_lock, flags);
	}

	skb = skb_dequeue(&eptdev->queue);
	spin_unlock_irqrestore(&eptdev->queue_lock, flags);
	if (!skb)
		return -EFAULT;

	use = min_t(size_t, iov_iter_count(to), skb->len);
	if (copy_to_iter(skb->data, use, to) != use)
		use = -EFAULT;

	kfree_skb(skb);

	return use;
}

static ssize_t rpmsg_eptdev_write_iter(struct kiocb *iocb,
				       struct iov_iter *from)
{
	struct file *filp = iocb->ki_filp;
	struct rpmsg_eptdev *eptdev = filp->private_data;
	size_t len = iov_iter_count(from);
	void *kbuf;
	int ret;

	kbuf = kzalloc(len, GFP_KERNEL);
	if (!kbuf)
		return -ENOMEM;

	if (!copy_from_iter_full(kbuf, len, from)) {
		ret = -EFAULT;
		goto free_kbuf;
	}

	if (mutex_lock_interruptible(&eptdev->ept_lock)) {
		ret = -ERESTARTSYS;
		goto free_kbuf;
	}

	if (!eptdev->ept) {
		ret = -EPIPE;
		goto unlock_eptdev;
	}

	if (filp->f_flags & O_NONBLOCK) {
		ret = rpmsg_trysendto(eptdev->ept, kbuf, len, eptdev->chinfo.dst);
		if (ret == -ENOMEM)
			ret = -EAGAIN;
	} else {
		ret = rpmsg_sendto(eptdev->ept, kbuf, len, eptdev->chinfo.dst);
	}

unlock_eptdev:
	mutex_unlock(&eptdev->ept_lock);

free_kbuf:
	kfree(kbuf);
	return ret < 0 ? ret : len;
}

static __poll_t rpmsg_eptdev_poll(struct file *filp, poll_table *wait)
{
	struct rpmsg_eptdev *eptdev = filp->private_data;
	__poll_t mask = 0;

	if (!eptdev->ept)
		return EPOLLERR;

	poll_wait(filp, &eptdev->readq, wait);

	if (!skb_queue_empty(&eptdev->queue))
		mask |= EPOLLIN | EPOLLRDNORM;

	if (eptdev->remote_flow_updated)
		mask |= EPOLLPRI;

	mutex_lock(&eptdev->ept_lock);
	mask |= rpmsg_poll(eptdev->ept, filp, wait);
	mutex_unlock(&eptdev->ept_lock);

	return mask;
}

static long rpmsg_eptdev_ioctl(struct file *fp, unsigned int cmd,
			       unsigned long arg)
{
	struct rpmsg_eptdev *eptdev = fp->private_data;

	bool set;
	int ret;

	switch (cmd) {
	case RPMSG_GET_OUTGOING_FLOWCONTROL:
		eptdev->remote_flow_updated = false;
		ret = put_user(eptdev->remote_flow_restricted, (int __user *)arg);
		break;
	case RPMSG_SET_INCOMING_FLOWCONTROL:
		if (arg > 1) {
			ret = -EINVAL;
			break;
		}
		set = !!arg;
		ret = rpmsg_set_flow_control(eptdev->ept, set, eptdev->chinfo.dst);
		break;
	case RPMSG_DESTROY_EPT_IOCTL:
		/* Don't allow to destroy a default endpoint. */
		if (eptdev->default_ept) {
			ret = -EINVAL;
			break;
		}
		ret = rpmsg_chrdev_eptdev_destroy(&eptdev->dev, NULL);
		break;
	default:
		ret = -EINVAL;
	}

	return ret;
}

static const struct file_operations rpmsg_eptdev_fops = {
	.owner = THIS_MODULE,
	.open = rpmsg_eptdev_open,
	.release = rpmsg_eptdev_release,
	.read_iter = rpmsg_eptdev_read_iter,
	.write_iter = rpmsg_eptdev_write_iter,
	.poll = rpmsg_eptdev_poll,
	.unlocked_ioctl = rpmsg_eptdev_ioctl,
	.compat_ioctl = compat_ptr_ioctl,
};

static ssize_t name_show(struct device *dev, struct device_attribute *attr,
			 char *buf)
{
	struct rpmsg_eptdev *eptdev = dev_get_drvdata(dev);

	return sprintf(buf, "%s\n", eptdev->chinfo.name);
}
static DEVICE_ATTR_RO(name);

static ssize_t src_show(struct device *dev, struct device_attribute *attr,
			 char *buf)
{
	struct rpmsg_eptdev *eptdev = dev_get_drvdata(dev);

	return sprintf(buf, "%d\n", eptdev->chinfo.src);
}
static DEVICE_ATTR_RO(src);

static ssize_t dst_show(struct device *dev, struct device_attribute *attr,
			 char *buf)
{
	struct rpmsg_eptdev *eptdev = dev_get_drvdata(dev);

	return sprintf(buf, "%d\n", eptdev->chinfo.dst);
}
static DEVICE_ATTR_RO(dst);

static struct attribute *rpmsg_eptdev_attrs[] = {
	&dev_attr_name.attr,
	&dev_attr_src.attr,
	&dev_attr_dst.attr,
	NULL
};
ATTRIBUTE_GROUPS(rpmsg_eptdev);

static void rpmsg_eptdev_release_device(struct device *dev)
{
	struct rpmsg_eptdev *eptdev = dev_to_eptdev(dev);

	ida_free(&rpmsg_ept_ida, dev->id);
	ida_free(&rpmsg_minor_ida, MINOR(eptdev->dev.devt));
	kfree(eptdev);
}

static struct rpmsg_eptdev *rpmsg_chrdev_eptdev_alloc(struct rpmsg_device *rpdev,
						      struct device *parent)
{
	struct rpmsg_eptdev *eptdev;
	struct device *dev;

	eptdev = kzalloc(sizeof(*eptdev), GFP_KERNEL);
	if (!eptdev)
		return ERR_PTR(-ENOMEM);

	dev = &eptdev->dev;
	eptdev->rpdev = rpdev;

	mutex_init(&eptdev->ept_lock);
	spin_lock_init(&eptdev->queue_lock);
	skb_queue_head_init(&eptdev->queue);
	init_waitqueue_head(&eptdev->readq);

	device_initialize(dev);
	dev->class = &rpmsg_class;
	dev->parent = parent;
	dev->groups = rpmsg_eptdev_groups;
	dev_set_drvdata(dev, eptdev);

	cdev_init(&eptdev->cdev, &rpmsg_eptdev_fops);
	eptdev->cdev.owner = THIS_MODULE;

	return eptdev;
}

static int rpmsg_chrdev_eptdev_add(struct rpmsg_eptdev *eptdev, struct rpmsg_channel_info chinfo)
{
	struct device *dev = &eptdev->dev;
	int ret;

	eptdev->chinfo = chinfo;

	ret = ida_alloc_max(&rpmsg_minor_ida, RPMSG_DEV_MAX - 1, GFP_KERNEL);
	if (ret < 0)
		goto free_eptdev;
	dev->devt = MKDEV(MAJOR(rpmsg_major), ret);

	ret = ida_alloc(&rpmsg_ept_ida, GFP_KERNEL);
	if (ret < 0)
		goto free_minor_ida;
	dev->id = ret;
	dev_set_name(dev, "rpmsg%d", ret);

	ret = cdev_device_add(&eptdev->cdev, &eptdev->dev);
	if (ret)
		goto free_ept_ida;

	/* We can now rely on the release function for cleanup */
	dev->release = rpmsg_eptdev_release_device;

	return ret;

free_ept_ida:
	ida_free(&rpmsg_ept_ida, dev->id);
free_minor_ida:
	ida_free(&rpmsg_minor_ida, MINOR(dev->devt));
free_eptdev:
	put_device(dev);
	kfree(eptdev);

	return ret;
}

int rpmsg_chrdev_eptdev_create(struct rpmsg_device *rpdev, struct device *parent,
			       struct rpmsg_channel_info chinfo)
{
	struct rpmsg_eptdev *eptdev;

	eptdev = rpmsg_chrdev_eptdev_alloc(rpdev, parent);
	if (IS_ERR(eptdev))
		return PTR_ERR(eptdev);

	return rpmsg_chrdev_eptdev_add(eptdev, chinfo);
}
EXPORT_SYMBOL(rpmsg_chrdev_eptdev_create);

static int rpmsg_chrdev_probe(struct rpmsg_device *rpdev)
{
	struct rpmsg_channel_info chinfo;
	struct rpmsg_eptdev *eptdev;
	struct device *dev = &rpdev->dev;

	memcpy(chinfo.name, rpdev->id.name, RPMSG_NAME_SIZE);
	chinfo.src = rpdev->src;
	chinfo.dst = rpdev->dst;

	eptdev = rpmsg_chrdev_eptdev_alloc(rpdev, dev);
	if (IS_ERR(eptdev))
		return PTR_ERR(eptdev);

	/* Set the default_ept to the rpmsg device endpoint */
	eptdev->default_ept = rpdev->ept;

	/*
	 * The rpmsg_ept_cb uses *priv parameter to get its rpmsg_eptdev context.
	 * Storedit in default_ept *priv field.
	 */
	eptdev->default_ept->priv = eptdev;

	return rpmsg_chrdev_eptdev_add(eptdev, chinfo);
}

static void rpmsg_chrdev_remove(struct rpmsg_device *rpdev)
{
	int ret;

	ret = device_for_each_child(&rpdev->dev, NULL, rpmsg_chrdev_eptdev_destroy);
	if (ret)
		dev_warn(&rpdev->dev, "failed to destroy endpoints: %d\n", ret);
}

static struct rpmsg_device_id rpmsg_chrdev_id_table[] = {
	{ .name	= "rpmsg-raw" },
	{ .name	= "rpmsg_chrdev" },
	{ },
};
MODULE_DEVICE_TABLE(rpmsg, rpmsg_chrdev_id_table);

static struct rpmsg_driver rpmsg_chrdev_driver = {
	.probe = rpmsg_chrdev_probe,
	.remove = rpmsg_chrdev_remove,
	.callback = rpmsg_ept_cb,
	.id_table = rpmsg_chrdev_id_table,
	.drv.name = "rpmsg_chrdev",
};

static int rpmsg_chrdev_init(void)
{
	int ret;

	ret = alloc_chrdev_region(&rpmsg_major, 0, RPMSG_DEV_MAX, "rpmsg_char");
	if (ret < 0) {
		pr_err("failed to allocate char dev region\n");
		return ret;
	}

	ret = register_rpmsg_driver(&rpmsg_chrdev_driver);
	if (ret < 0) {
		pr_err("rpmsg: failed to register rpmsg raw driver\n");
		goto free_region;
	}

	return 0;

free_region:
	unregister_chrdev_region(rpmsg_major, RPMSG_DEV_MAX);

	return ret;
}
postcore_initcall(rpmsg_chrdev_init);

static void rpmsg_chrdev_exit(void)
{
	unregister_rpmsg_driver(&rpmsg_chrdev_driver);
	unregister_chrdev_region(rpmsg_major, RPMSG_DEV_MAX);
}
module_exit(rpmsg_chrdev_exit);

MODULE_DESCRIPTION("RPMSG device interface");
MODULE_LICENSE("GPL v2");
