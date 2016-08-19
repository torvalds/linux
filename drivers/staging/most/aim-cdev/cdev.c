/*
 * cdev.c - Application interfacing module for character devices
 *
 * Copyright (C) 2013-2015 Microchip Technology Germany II GmbH & Co. KG
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * This file is licensed under GPLv2.
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
#include "mostcore.h"

static dev_t aim_devno;
static struct class *aim_class;
static struct ida minor_id;
static unsigned int major;
static struct most_aim cdev_aim;

struct aim_channel {
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

#define to_channel(d) container_of(d, struct aim_channel, cdev)
static struct list_head channel_list;
static spinlock_t ch_list_lock;

static inline bool ch_has_mbo(struct aim_channel *c)
{
	return channel_has_mbo(c->iface, c->channel_id, &cdev_aim) > 0;
}

static inline bool ch_get_mbo(struct aim_channel *c, struct mbo **mbo)
{
	*mbo = most_get_mbo(c->iface, c->channel_id, &cdev_aim);
	return *mbo;
}

static struct aim_channel *get_channel(struct most_interface *iface, int id)
{
	struct aim_channel *c, *tmp;
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

static void stop_channel(struct aim_channel *c)
{
	struct mbo *mbo;

	while (kfifo_out((struct kfifo *)&c->fifo, &mbo, 1))
		most_put_mbo(mbo);
	most_stop_channel(c->iface, c->channel_id, &cdev_aim);
}

static void destroy_cdev(struct aim_channel *c)
{
	unsigned long flags;

	device_destroy(aim_class, c->devno);
	cdev_del(&c->cdev);
	kfifo_free(&c->fifo);
	spin_lock_irqsave(&ch_list_lock, flags);
	list_del(&c->list);
	spin_unlock_irqrestore(&ch_list_lock, flags);
	ida_simple_remove(&minor_id, MINOR(c->devno));
}

/**
 * aim_open - implements the syscall to open the device
 * @inode: inode pointer
 * @filp: file pointer
 *
 * This stores the channel pointer in the private data field of
 * the file structure and activates the channel within the core.
 */
static int aim_open(struct inode *inode, struct file *filp)
{
	struct aim_channel *c;
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
	ret = most_start_channel(c->iface, c->channel_id, &cdev_aim);
	if (!ret)
		c->access_ref = 1;
	mutex_unlock(&c->io_mutex);
	return ret;
}

/**
 * aim_close - implements the syscall to close the device
 * @inode: inode pointer
 * @filp: file pointer
 *
 * This stops the channel within the core.
 */
static int aim_close(struct inode *inode, struct file *filp)
{
	struct aim_channel *c = to_channel(inode->i_cdev);

	mutex_lock(&c->io_mutex);
	spin_lock(&c->unlink);
	c->access_ref = 0;
	spin_unlock(&c->unlink);
	if (c->dev) {
		stop_channel(c);
		mutex_unlock(&c->io_mutex);
	} else {
		destroy_cdev(c);
		mutex_unlock(&c->io_mutex);
		kfree(c);
	}
	return 0;
}

/**
 * aim_write - implements the syscall to write to the device
 * @filp: file pointer
 * @buf: pointer to user buffer
 * @count: number of bytes to write
 * @offset: offset from where to start writing
 */
static ssize_t aim_write(struct file *filp, const char __user *buf,
			 size_t count, loff_t *offset)
{
	int ret;
	size_t actual_len;
	size_t max_len;
	struct mbo *mbo = NULL;
	struct aim_channel *c = filp->private_data;

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

	max_len = c->cfg->buffer_size;
	actual_len = min(count, max_len);
	mbo->buffer_length = actual_len;

	if (copy_from_user(mbo->virt_address, buf, mbo->buffer_length)) {
		ret = -EFAULT;
		goto put_mbo;
	}

	ret = most_submit_mbo(mbo);
	if (ret)
		goto put_mbo;

	mutex_unlock(&c->io_mutex);
	return actual_len;
put_mbo:
	most_put_mbo(mbo);
unlock:
	mutex_unlock(&c->io_mutex);
	return ret;
}

/**
 * aim_read - implements the syscall to read from the device
 * @filp: file pointer
 * @buf: pointer to user buffer
 * @count: number of bytes to read
 * @offset: offset from where to start reading
 */
static ssize_t
aim_read(struct file *filp, char __user *buf, size_t count, loff_t *offset)
{
	size_t to_copy, not_copied, copied;
	struct mbo *mbo;
	struct aim_channel *c = filp->private_data;

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

static unsigned int aim_poll(struct file *filp, poll_table *wait)
{
	struct aim_channel *c = filp->private_data;
	unsigned int mask = 0;

	poll_wait(filp, &c->wq, wait);

	if (c->cfg->direction == MOST_CH_RX) {
		if (!kfifo_is_empty(&c->fifo))
			mask |= POLLIN | POLLRDNORM;
	} else {
		if (ch_has_mbo(c))
			mask |= POLLOUT | POLLWRNORM;
	}
	return mask;
}

/**
 * Initialization of struct file_operations
 */
static const struct file_operations channel_fops = {
	.owner = THIS_MODULE,
	.read = aim_read,
	.write = aim_write,
	.open = aim_open,
	.release = aim_close,
	.poll = aim_poll,
};

/**
 * aim_disconnect_channel - disconnect a channel
 * @iface: pointer to interface instance
 * @channel_id: channel index
 *
 * This frees allocated memory and removes the cdev that represents this
 * channel in user space.
 */
static int aim_disconnect_channel(struct most_interface *iface, int channel_id)
{
	struct aim_channel *c;

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
	if (c->access_ref) {
		stop_channel(c);
		wake_up_interruptible(&c->wq);
		mutex_unlock(&c->io_mutex);
	} else {
		destroy_cdev(c);
		mutex_unlock(&c->io_mutex);
		kfree(c);
	}
	return 0;
}

/**
 * aim_rx_completion - completion handler for rx channels
 * @mbo: pointer to buffer object that has completed
 *
 * This searches for the channel linked to this MBO and stores it in the local
 * fifo buffer.
 */
static int aim_rx_completion(struct mbo *mbo)
{
	struct aim_channel *c;

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
 * aim_tx_completion - completion handler for tx channels
 * @iface: pointer to interface instance
 * @channel_id: channel index/ID
 *
 * This wakes sleeping processes in the wait-queue.
 */
static int aim_tx_completion(struct most_interface *iface, int channel_id)
{
	struct aim_channel *c;

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
 * aim_probe - probe function of the driver module
 * @iface: pointer to interface instance
 * @channel_id: channel index/ID
 * @cfg: pointer to actual channel configuration
 * @parent: pointer to kobject (needed for sysfs hook-up)
 * @name: name of the device to be created
 *
 * This allocates achannel object and creates the device node in /dev
 *
 * Returns 0 on success or error code otherwise.
 */
static int aim_probe(struct most_interface *iface, int channel_id,
		     struct most_channel_config *cfg,
		     struct kobject *parent, char *name)
{
	struct aim_channel *c;
	unsigned long cl_flags;
	int retval;
	int current_minor;

	if ((!iface) || (!cfg) || (!parent) || (!name)) {
		pr_info("Probing AIM with bad arguments");
		return -EINVAL;
	}
	c = get_channel(iface, channel_id);
	if (c)
		return -EEXIST;

	current_minor = ida_simple_get(&minor_id, 0, 0, GFP_KERNEL);
	if (current_minor < 0)
		return current_minor;

	c = kzalloc(sizeof(*c), GFP_KERNEL);
	if (!c) {
		retval = -ENOMEM;
		goto error_alloc_channel;
	}

	c->devno = MKDEV(major, current_minor);
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
	c->dev = device_create(aim_class,
				     NULL,
				     c->devno,
				     NULL,
				     "%s", name);

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
	ida_simple_remove(&minor_id, current_minor);
	return retval;
}

static struct most_aim cdev_aim = {
	.name = "cdev",
	.probe_channel = aim_probe,
	.disconnect_channel = aim_disconnect_channel,
	.rx_completion = aim_rx_completion,
	.tx_completion = aim_tx_completion,
};

static int __init mod_init(void)
{
	int err;

	pr_info("init()\n");

	INIT_LIST_HEAD(&channel_list);
	spin_lock_init(&ch_list_lock);
	ida_init(&minor_id);

	err = alloc_chrdev_region(&aim_devno, 0, 50, "cdev");
	if (err < 0)
		return err;
	major = MAJOR(aim_devno);

	aim_class = class_create(THIS_MODULE, "most_cdev_aim");
	if (IS_ERR(aim_class)) {
		pr_err("no udev support\n");
		err = PTR_ERR(aim_class);
		goto free_cdev;
	}
	err = most_register_aim(&cdev_aim);
	if (err)
		goto dest_class;
	return 0;

dest_class:
	class_destroy(aim_class);
free_cdev:
	unregister_chrdev_region(aim_devno, 1);
	return err;
}

static void __exit mod_exit(void)
{
	struct aim_channel *c, *tmp;

	pr_info("exit module\n");

	most_deregister_aim(&cdev_aim);

	list_for_each_entry_safe(c, tmp, &channel_list, list) {
		destroy_cdev(c);
		kfree(c);
	}
	class_destroy(aim_class);
	unregister_chrdev_region(aim_devno, 1);
	ida_destroy(&minor_id);
}

module_init(mod_init);
module_exit(mod_exit);
MODULE_AUTHOR("Christian Gromm <christian.gromm@microchip.com>");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("character device AIM for mostcore");
