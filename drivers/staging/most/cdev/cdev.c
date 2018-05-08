// SPDX-License-Identifier: GPL-2.0
/*
 * cdev.c - Character device component for Mostcore
 *
 * Copyright (C) 2013-2015 Microchip Technology Germany II GmbH & Co. KG
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt
#include <linux/module.h>
#include <linux/sched.h>
#include <linux/fs.h>
#include <linux/slab.h>
#include <linux/device.h>
#include <linux/cdev.h>
#include <linux/poll.h>
#include <linux/kfifo.h>
#include <linux/uaccess.h>
#include <linux/idr.h>
#include "most/core.h"

#define CHRDEV_REGION_SIZE 50

static struct cdev_component {
	dev_t devno;
	struct ida minor_id;
	unsigned int major;
	struct class *class;
	struct core_component cc;
} comp;

struct comp_channel {
	wait_queue_head_t wq;
	spinlock_t unlink;	/* synchronization lock to unlink channels */
	struct cdev cdev;
	struct device *dev;
	struct mutex io_mutex;
	struct most_interface *iface;
	struct most_channel_config *cfg;
	unsigned int channel_id;
	dev_t devno;
	size_t mbo_offs;
	DECLARE_KFIFO_PTR(fifo, typeof(struct mbo *));
	int access_ref;
	struct list_head list;
};

#define to_channel(d) container_of(d, struct comp_channel, cdev)
static struct list_head channel_list;
static spinlock_t ch_list_lock;

static inline bool ch_has_mbo(struct comp_channel *c)
{
	return channel_has_mbo(c->iface, c->channel_id, &comp.cc) > 0;
}

static inline struct mbo *ch_get_mbo(struct comp_channel *c, struct mbo **mbo)
{
	if (!kfifo_peek(&c->fifo, mbo)) {
		*mbo = most_get_mbo(c->iface, c->channel_id, &comp.cc);
		if (*mbo)
			kfifo_in(&c->fifo, mbo, 1);
	}
	return *mbo;
}

static struct comp_channel *get_channel(struct most_interface *iface, int id)
{
	struct comp_channel *c, *tmp;
	unsigned long flags;
	int found_channel = 0;

	spin_lock_irqsave(&ch_list_lock, flags);
	list_for_each_entry_safe(c, tmp, &channel_list, list) {
		if ((c->iface == iface) && (c->channel_id == id)) {
			found_channel = 1;
			break;
		}
	}
	spin_unlock_irqrestore(&ch_list_lock, flags);
	if (!found_channel)
		return NULL;
	return c;
}

static void stop_channel(struct comp_channel *c)
{
	struct mbo *mbo;

	while (kfifo_out((struct kfifo *)&c->fifo, &mbo, 1))
		most_put_mbo(mbo);
	most_stop_channel(c->iface, c->channel_id, &comp.cc);
}

static void destroy_cdev(struct comp_channel *c)
{
	unsigned long flags;

	device_destroy(comp.class, c->devno);
	cdev_del(&c->cdev);
	spin_lock_irqsave(&ch_list_lock, flags);
	list_del(&c->list);
	spin_unlock_irqrestore(&ch_list_lock, flags);
}

static void destroy_channel(struct comp_channel *c)
{
	ida_simple_remove(&comp.minor_id, MINOR(c->devno));
	kfifo_free(&c->fifo);
	kfree(c);
}

/**
 * comp_open - implements the syscall to open the device
 * @inode: inode pointer
 * @filp: file pointer
 *
 * This stores the channel pointer in the private data field of
 * the file structure and activates the channel within the core.
 */
static int comp_open(struct inode *inode, struct file *filp)
{
	struct comp_channel *c;
	int ret;

	c = to_channel(inode->i_cdev);
	filp->private_data = c;

	if (((c->cfg->direction == MOST_CH_RX) &&
	     ((filp->f_flags & O_ACCMODE) != O_RDONLY)) ||
	     ((c->cfg->direction == MOST_CH_TX) &&
		((filp->f_flags & O_ACCMODE) != O_WRONLY))) {
		pr_info("WARN: Access flags mismatch\n");
		return -EACCES;
	}

	mutex_lock(&c->io_mutex);
	if (!c->dev) {
		pr_info("WARN: Device is destroyed\n");
		mutex_unlock(&c->io_mutex);
		return -ENODEV;
	}

	if (c->access_ref) {
		pr_info("WARN: Device is busy\n");
		mutex_unlock(&c->io_mutex);
		return -EBUSY;
	}

	c->mbo_offs = 0;
	ret = most_start_channel(c->iface, c->channel_id, &comp.cc);
	if (!ret)
		c->access_ref = 1;
	mutex_unlock(&c->io_mutex);
	return ret;
}

/**
 * comp_close - implements the syscall to close the device
 * @inode: inode pointer
 * @filp: file pointer
 *
 * This stops the channel within the core.
 */
static int comp_close(struct inode *inode, struct file *filp)
{
	struct comp_channel *c = to_channel(inode->i_cdev);

	mutex_lock(&c->io_mutex);
	spin_lock(&c->unlink);
	c->access_ref = 0;
	spin_unlock(&c->unlink);
	if (c->dev) {
		stop_channel(c);
		mutex_unlock(&c->io_mutex);
	} else {
		mutex_unlock(&c->io_mutex);
		destroy_channel(c);
	}
	return 0;
}

/**
 * comp_write - implements the syscall to write to the device
 * @filp: file pointer
 * @buf: pointer to user buffer
 * @count: number of bytes to write
 * @offset: offset from where to start writing
 */
static ssize_t comp_write(struct file *filp, const char __user *buf,
			  size_t count, loff_t *offset)
{
	int ret;
	size_t to_copy, left;
	struct mbo *mbo = NULL;
	struct comp_channel *c = filp->private_data;

	mutex_lock(&c->io_mutex);
	while (c->dev && !ch_get_mbo(c, &mbo)) {
		mutex_unlock(&c->io_mutex);

		if ((filp->f_flags & O_NONBLOCK))
			return -EAGAIN;
		if (wait_event_interruptible(c->wq, ch_has_mbo(c) || !c->dev))
			return -ERESTARTSYS;
		mutex_lock(&c->io_mutex);
	}

	if (unlikely(!c->dev)) {
		ret = -ENODEV;
		goto unlock;
	}

	to_copy = min(count, c->cfg->buffer_size - c->mbo_offs);
	left = copy_from_user(mbo->virt_address + c->mbo_offs, buf, to_copy);
	if (left == to_copy) {
		ret = -EFAULT;
		goto unlock;
	}

	c->mbo_offs += to_copy - left;
	if (c->mbo_offs >= c->cfg->buffer_size ||
	    c->cfg->data_type == MOST_CH_CONTROL ||
	    c->cfg->data_type == MOST_CH_ASYNC) {
		kfifo_skip(&c->fifo);
		mbo->buffer_length = c->mbo_offs;
		c->mbo_offs = 0;
		most_submit_mbo(mbo);
	}

	ret = to_copy - left;
unlock:
	mutex_unlock(&c->io_mutex);
	return ret;
}

/**
 * comp_read - implements the syscall to read from the device
 * @filp: file pointer
 * @buf: pointer to user buffer
 * @count: number of bytes to read
 * @offset: offset from where to start reading
 */
static ssize_t
comp_read(struct file *filp, char __user *buf, size_t count, loff_t *offset)
{
	size_t to_copy, not_copied, copied;
	struct mbo *mbo = NULL;
	struct comp_channel *c = filp->private_data;

	mutex_lock(&c->io_mutex);
	while (c->dev && !kfifo_peek(&c->fifo, &mbo)) {
		mutex_unlock(&c->io_mutex);
		if (filp->f_flags & O_NONBLOCK)
			return -EAGAIN;
		if (wait_event_interruptible(c->wq,
					     (!kfifo_is_empty(&c->fifo) ||
					      (!c->dev))))
			return -ERESTARTSYS;
		mutex_lock(&c->io_mutex);
	}

	/* make sure we don't submit to gone devices */
	if (unlikely(!c->dev)) {
		mutex_unlock(&c->io_mutex);
		return -ENODEV;
	}

	to_copy = min_t(size_t,
			count,
			mbo->processed_length - c->mbo_offs);

	not_copied = copy_to_user(buf,
				  mbo->virt_address + c->mbo_offs,
				  to_copy);

	copied = to_copy - not_copied;

	c->mbo_offs += copied;
	if (c->mbo_offs >= mbo->processed_length) {
		kfifo_skip(&c->fifo);
		most_put_mbo(mbo);
		c->mbo_offs = 0;
	}
	mutex_unlock(&c->io_mutex);
	return copied;
}

static __poll_t comp_poll(struct file *filp, poll_table *wait)
{
	struct comp_channel *c = filp->private_data;
	__poll_t mask = 0;

	poll_wait(filp, &c->wq, wait);

	mutex_lock(&c->io_mutex);
	if (c->cfg->direction == MOST_CH_RX) {
		if (!c->dev || !kfifo_is_empty(&c->fifo))
			mask |= EPOLLIN | EPOLLRDNORM;
	} else {
		if (!c->dev || !kfifo_is_empty(&c->fifo) || ch_has_mbo(c))
			mask |= EPOLLOUT | EPOLLWRNORM;
	}
	mutex_unlock(&c->io_mutex);
	return mask;
}

/**
 * Initialization of struct file_operations
 */
static const struct file_operations channel_fops = {
	.owner = THIS_MODULE,
	.read = comp_read,
	.write = comp_write,
	.open = comp_open,
	.release = comp_close,
	.poll = comp_poll,
};

/**
 * comp_disconnect_channel - disconnect a channel
 * @iface: pointer to interface instance
 * @channel_id: channel index
 *
 * This frees allocated memory and removes the cdev that represents this
 * channel in user space.
 */
static int comp_disconnect_channel(struct most_interface *iface, int channel_id)
{
	struct comp_channel *c;

	if (!iface) {
		pr_info("Bad interface pointer\n");
		return -EINVAL;
	}

	c = get_channel(iface, channel_id);
	if (!c)
		return -ENXIO;

	mutex_lock(&c->io_mutex);
	spin_lock(&c->unlink);
	c->dev = NULL;
	spin_unlock(&c->unlink);
	destroy_cdev(c);
	if (c->access_ref) {
		stop_channel(c);
		wake_up_interruptible(&c->wq);
		mutex_unlock(&c->io_mutex);
	} else {
		mutex_unlock(&c->io_mutex);
		destroy_channel(c);
	}
	return 0;
}

/**
 * comp_rx_completion - completion handler for rx channels
 * @mbo: pointer to buffer object that has completed
 *
 * This searches for the channel linked to this MBO and stores it in the local
 * fifo buffer.
 */
static int comp_rx_completion(struct mbo *mbo)
{
	struct comp_channel *c;

	if (!mbo)
		return -EINVAL;

	c = get_channel(mbo->ifp, mbo->hdm_channel_id);
	if (!c)
		return -ENXIO;

	spin_lock(&c->unlink);
	if (!c->access_ref || !c->dev) {
		spin_unlock(&c->unlink);
		return -ENODEV;
	}
	kfifo_in(&c->fifo, &mbo, 1);
	spin_unlock(&c->unlink);
#ifdef DEBUG_MESG
	if (kfifo_is_full(&c->fifo))
		pr_info("WARN: Fifo is full\n");
#endif
	wake_up_interruptible(&c->wq);
	return 0;
}

/**
 * comp_tx_completion - completion handler for tx channels
 * @iface: pointer to interface instance
 * @channel_id: channel index/ID
 *
 * This wakes sleeping processes in the wait-queue.
 */
static int comp_tx_completion(struct most_interface *iface, int channel_id)
{
	struct comp_channel *c;

	if (!iface) {
		pr_info("Bad interface pointer\n");
		return -EINVAL;
	}
	if ((channel_id < 0) || (channel_id >= iface->num_channels)) {
		pr_info("Channel ID out of range\n");
		return -EINVAL;
	}

	c = get_channel(iface, channel_id);
	if (!c)
		return -ENXIO;
	wake_up_interruptible(&c->wq);
	return 0;
}

/**
 * comp_probe - probe function of the driver module
 * @iface: pointer to interface instance
 * @channel_id: channel index/ID
 * @cfg: pointer to actual channel configuration
 * @name: name of the device to be created
 *
 * This allocates achannel object and creates the device node in /dev
 *
 * Returns 0 on success or error code otherwise.
 */
static int comp_probe(struct most_interface *iface, int channel_id,
		      struct most_channel_config *cfg, char *name)
{
	struct comp_channel *c;
	unsigned long cl_flags;
	int retval;
	int current_minor;

	if ((!iface) || (!cfg) || (!name)) {
		pr_info("Probing component with bad arguments");
		return -EINVAL;
	}
	c = get_channel(iface, channel_id);
	if (c)
		return -EEXIST;

	current_minor = ida_simple_get(&comp.minor_id, 0, 0, GFP_KERNEL);
	if (current_minor < 0)
		return current_minor;

	c = kzalloc(sizeof(*c), GFP_KERNEL);
	if (!c) {
		retval = -ENOMEM;
		goto error_alloc_channel;
	}

	c->devno = MKDEV(comp.major, current_minor);
	cdev_init(&c->cdev, &channel_fops);
	c->cdev.owner = THIS_MODULE;
	cdev_add(&c->cdev, c->devno, 1);
	c->iface = iface;
	c->cfg = cfg;
	c->channel_id = channel_id;
	c->access_ref = 0;
	spin_lock_init(&c->unlink);
	INIT_KFIFO(c->fifo);
	retval = kfifo_alloc(&c->fifo, cfg->num_buffers, GFP_KERNEL);
	if (retval) {
		pr_info("failed to alloc channel kfifo");
		goto error_alloc_kfifo;
	}
	init_waitqueue_head(&c->wq);
	mutex_init(&c->io_mutex);
	spin_lock_irqsave(&ch_list_lock, cl_flags);
	list_add_tail(&c->list, &channel_list);
	spin_unlock_irqrestore(&ch_list_lock, cl_flags);
	c->dev = device_create(comp.class, NULL, c->devno, NULL, "%s", name);

	if (IS_ERR(c->dev)) {
		retval = PTR_ERR(c->dev);
		pr_info("failed to create new device node %s\n", name);
		goto error_create_device;
	}
	kobject_uevent(&c->dev->kobj, KOBJ_ADD);
	return 0;

error_create_device:
	kfifo_free(&c->fifo);
	list_del(&c->list);
error_alloc_kfifo:
	cdev_del(&c->cdev);
	kfree(c);
error_alloc_channel:
	ida_simple_remove(&comp.minor_id, current_minor);
	return retval;
}

static struct cdev_component comp = {
	.cc = {
		.name = "cdev",
		.probe_channel = comp_probe,
		.disconnect_channel = comp_disconnect_channel,
		.rx_completion = comp_rx_completion,
		.tx_completion = comp_tx_completion,
	},
};

static int __init mod_init(void)
{
	int err;

	pr_info("init()\n");

	comp.class = class_create(THIS_MODULE, "most_cdev");
	if (IS_ERR(comp.class)) {
		pr_info("No udev support.\n");
		return PTR_ERR(comp.class);
	}

	INIT_LIST_HEAD(&channel_list);
	spin_lock_init(&ch_list_lock);
	ida_init(&comp.minor_id);

	err = alloc_chrdev_region(&comp.devno, 0, CHRDEV_REGION_SIZE, "cdev");
	if (err < 0)
		goto dest_ida;
	comp.major = MAJOR(comp.devno);
	err = most_register_component(&comp.cc);
	if (err)
		goto free_cdev;
	return 0;

free_cdev:
	unregister_chrdev_region(comp.devno, CHRDEV_REGION_SIZE);
dest_ida:
	ida_destroy(&comp.minor_id);
	class_destroy(comp.class);
	return err;
}

static void __exit mod_exit(void)
{
	struct comp_channel *c, *tmp;

	pr_info("exit module\n");

	most_deregister_component(&comp.cc);

	list_for_each_entry_safe(c, tmp, &channel_list, list) {
		destroy_cdev(c);
		destroy_channel(c);
	}
	unregister_chrdev_region(comp.devno, 1);
	ida_destroy(&comp.minor_id);
	class_destroy(comp.class);
}

module_init(mod_init);
module_exit(mod_exit);
MODULE_AUTHOR("Christian Gromm <christian.gromm@microchip.com>");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("character device component for mostcore");
