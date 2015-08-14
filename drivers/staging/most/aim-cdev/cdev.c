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
#include <linux/kfifo.h>
#include <linux/uaccess.h>
#include <linux/idr.h>
#include "mostcore.h"

static dev_t aim_devno;
static struct class *aim_class;
static struct ida minor_id;
static unsigned int major;

struct aim_channel {
	wait_queue_head_t wq;
	struct cdev cdev;
	struct device *dev;
	struct mutex io_mutex;
	struct most_interface *iface;
	struct most_channel_config *cfg;
	unsigned int channel_id;
	dev_t devno;
	bool keep_mbo;
	unsigned int mbo_offs;
	struct mbo *stacked_mbo;
	DECLARE_KFIFO_PTR(fifo, typeof(struct mbo *));
	atomic_t access_ref;
	struct list_head list;
};
#define to_channel(d) container_of(d, struct aim_channel, cdev)
static struct list_head channel_list;
static spinlock_t ch_list_lock;


static struct aim_channel *get_channel(struct most_interface *iface, int id)
{
	struct aim_channel *channel, *tmp;
	unsigned long flags;
	int found_channel = 0;

	spin_lock_irqsave(&ch_list_lock, flags);
	list_for_each_entry_safe(channel, tmp, &channel_list, list) {
		if ((channel->iface == iface) && (channel->channel_id == id)) {
			found_channel = 1;
			break;
		}
	}
	spin_unlock_irqrestore(&ch_list_lock, flags);
	if (!found_channel)
		return NULL;
	return channel;
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
	struct aim_channel *channel;
	int ret;

	channel = to_channel(inode->i_cdev);
	filp->private_data = channel;

	if (((channel->cfg->direction == MOST_CH_RX) &&
	     ((filp->f_flags & O_ACCMODE) != O_RDONLY))
	    || ((channel->cfg->direction == MOST_CH_TX) &&
		((filp->f_flags & O_ACCMODE) != O_WRONLY))) {
		pr_info("WARN: Access flags mismatch\n");
		return -EACCES;
	}
	if (!atomic_inc_and_test(&channel->access_ref)) {
		pr_info("WARN: Device is busy\n");
		atomic_dec(&channel->access_ref);
		return -EBUSY;
	}

	ret = most_start_channel(channel->iface, channel->channel_id);
	if (ret)
		atomic_dec(&channel->access_ref);
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
	int ret;
	struct mbo *mbo;
	struct aim_channel *channel = to_channel(inode->i_cdev);

	mutex_lock(&channel->io_mutex);
	if (!channel->dev) {
		mutex_unlock(&channel->io_mutex);
		atomic_dec(&channel->access_ref);
		device_destroy(aim_class, channel->devno);
		cdev_del(&channel->cdev);
		kfifo_free(&channel->fifo);
		list_del(&channel->list);
		ida_simple_remove(&minor_id, MINOR(channel->devno));
		wake_up_interruptible(&channel->wq);
		kfree(channel);
		return 0;
	}
	mutex_unlock(&channel->io_mutex);

	while (0 != kfifo_out((struct kfifo *)&channel->fifo, &mbo, 1))
		most_put_mbo(mbo);
	if (channel->keep_mbo == true)
		most_put_mbo(channel->stacked_mbo);
	ret = most_stop_channel(channel->iface, channel->channel_id);
	atomic_dec(&channel->access_ref);
	wake_up_interruptible(&channel->wq);
	return ret;
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
	int ret, err;
	size_t actual_len = 0;
	size_t max_len = 0;
	ssize_t retval;
	struct mbo *mbo;
	struct aim_channel *channel = filp->private_data;

	mutex_lock(&channel->io_mutex);
	if (unlikely(!channel->dev)) {
		mutex_unlock(&channel->io_mutex);
		return -EPIPE;
	}
	mutex_unlock(&channel->io_mutex);

	mbo = most_get_mbo(channel->iface, channel->channel_id);

	if (!mbo && channel->dev) {
		if ((filp->f_flags & O_NONBLOCK))
			return -EAGAIN;
		if (wait_event_interruptible(
			    channel->wq,
			    (mbo = most_get_mbo(channel->iface,
						channel->channel_id)) ||
			    (channel->dev == NULL)))
			return -ERESTARTSYS;
	}

	mutex_lock(&channel->io_mutex);
	if (unlikely(!channel->dev)) {
		mutex_unlock(&channel->io_mutex);
		err = -EPIPE;
		goto error;
	}
	mutex_unlock(&channel->io_mutex);

	max_len = channel->cfg->buffer_size;
	actual_len = min(count, max_len);
	mbo->buffer_length = actual_len;

	retval = copy_from_user(mbo->virt_address, buf, mbo->buffer_length);
	if (retval) {
		err = -EIO;
		goto error;
	}

	ret = most_submit_mbo(mbo);
	if (ret) {
		pr_info("submitting MBO to core failed\n");
		err = ret;
		goto error;
	}
	return actual_len - retval;
error:
	if (mbo)
		most_put_mbo(mbo);
	return err;
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
	ssize_t retval;
	size_t not_copied, proc_len;
	struct mbo *mbo;
	struct aim_channel *channel = filp->private_data;

	if (channel->keep_mbo == true) {
		mbo = channel->stacked_mbo;
		channel->keep_mbo = false;
		goto start_copy;
	}
	while ((0 == kfifo_out(&channel->fifo, &mbo, 1))
	       && (channel->dev != NULL)) {
		if (filp->f_flags & O_NONBLOCK)
			return -EAGAIN;
		if (wait_event_interruptible(channel->wq,
					     (!kfifo_is_empty(&channel->fifo) ||
					      (channel->dev == NULL))))
			return -ERESTARTSYS;
	}

start_copy:
	/* make sure we don't submit to gone devices */
	mutex_lock(&channel->io_mutex);
	if (unlikely(!channel->dev)) {
		mutex_unlock(&channel->io_mutex);
		return -EIO;
	}

	if (count < mbo->processed_length)
		channel->keep_mbo = true;

	proc_len = min((int)count,
		       (int)(mbo->processed_length - channel->mbo_offs));

	not_copied = copy_to_user(buf,
				  mbo->virt_address + channel->mbo_offs,
				  proc_len);

	retval = not_copied ? proc_len - not_copied : proc_len;

	if (channel->keep_mbo == true) {
		channel->mbo_offs = retval;
		channel->stacked_mbo = mbo;
	} else {
		most_put_mbo(mbo);
		channel->mbo_offs = 0;
	}
	mutex_unlock(&channel->io_mutex);
	return retval;
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
	struct aim_channel *channel;
	unsigned long flags;

	if (!iface) {
		pr_info("Bad interface pointer\n");
		return -EINVAL;
	}

	channel = get_channel(iface, channel_id);
	if (channel == NULL)
		return -ENXIO;

	mutex_lock(&channel->io_mutex);
	channel->dev = NULL;
	mutex_unlock(&channel->io_mutex);

	if (atomic_read(&channel->access_ref)) {
		device_destroy(aim_class, channel->devno);
		cdev_del(&channel->cdev);
		kfifo_free(&channel->fifo);
		ida_simple_remove(&minor_id, MINOR(channel->devno));
		spin_lock_irqsave(&ch_list_lock, flags);
		list_del(&channel->list);
		spin_unlock_irqrestore(&ch_list_lock, flags);
		kfree(channel);
	} else {
		wake_up_interruptible(&channel->wq);
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
	struct aim_channel *channel;

	if (!mbo)
		return -EINVAL;

	channel = get_channel(mbo->ifp, mbo->hdm_channel_id);
	if (channel == NULL)
		return -ENXIO;

	kfifo_in(&channel->fifo, &mbo, 1);
#ifdef DEBUG_MESG
	if (kfifo_is_full(&channel->fifo))
		pr_info("WARN: Fifo is full\n");
#endif
	wake_up_interruptible(&channel->wq);
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
	struct aim_channel *channel;

	if (!iface) {
		pr_info("Bad interface pointer\n");
		return -EINVAL;
	}
	if ((channel_id < 0) || (channel_id >= iface->num_channels)) {
		pr_info("Channel ID out of range\n");
		return -EINVAL;
	}

	channel = get_channel(iface, channel_id);
	if (channel == NULL)
		return -ENXIO;
	wake_up_interruptible(&channel->wq);
	return 0;
}

static struct most_aim cdev_aim;

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
	struct aim_channel *channel;
	unsigned long cl_flags;
	int retval;
	int current_minor;

	if ((!iface) || (!cfg) || (!parent) || (!name)) {
		pr_info("Probing AIM with bad arguments");
		return -EINVAL;
	}
	channel = get_channel(iface, channel_id);
	if (channel)
		return -EEXIST;

	current_minor = ida_simple_get(&minor_id, 0, 0, GFP_KERNEL);
	if (current_minor < 0)
		return current_minor;

	channel = kzalloc(sizeof(*channel), GFP_KERNEL);
	if (!channel) {
		pr_info("failed to alloc channel object\n");
		retval = -ENOMEM;
		goto error_alloc_channel;
	}

	channel->devno = MKDEV(major, current_minor);
	cdev_init(&channel->cdev, &channel_fops);
	channel->cdev.owner = THIS_MODULE;
	cdev_add(&channel->cdev, channel->devno, 1);
	channel->iface = iface;
	channel->cfg = cfg;
	channel->channel_id = channel_id;
	channel->mbo_offs = 0;
	atomic_set(&channel->access_ref, -1);
	INIT_KFIFO(channel->fifo);
	retval = kfifo_alloc(&channel->fifo, cfg->num_buffers, GFP_KERNEL);
	if (retval) {
		pr_info("failed to alloc channel kfifo");
		goto error_alloc_kfifo;
	}
	init_waitqueue_head(&channel->wq);
	mutex_init(&channel->io_mutex);
	spin_lock_irqsave(&ch_list_lock, cl_flags);
	list_add_tail(&channel->list, &channel_list);
	spin_unlock_irqrestore(&ch_list_lock, cl_flags);
	channel->dev = device_create(aim_class,
				     NULL,
				     channel->devno,
				     NULL,
				     "%s", name);

	retval = IS_ERR(channel->dev);
	if (retval) {
		pr_info("failed to create new device node %s\n", name);
		goto error_create_device;
	}
	kobject_uevent(&channel->dev->kobj, KOBJ_ADD);
	return 0;

error_create_device:
	kfifo_free(&channel->fifo);
	list_del(&channel->list);
error_alloc_kfifo:
	cdev_del(&channel->cdev);
	kfree(channel);
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
	pr_info("init()\n");

	INIT_LIST_HEAD(&channel_list);
	spin_lock_init(&ch_list_lock);
	ida_init(&minor_id);

	if (alloc_chrdev_region(&aim_devno, 0, 50, "cdev") < 0)
		return -EIO;
	major = MAJOR(aim_devno);

	aim_class = class_create(THIS_MODULE, "most_cdev_aim");
	if (IS_ERR(aim_class)) {
		pr_err("no udev support\n");
		goto free_cdev;
	}

	if (most_register_aim(&cdev_aim))
		goto dest_class;
	return 0;

dest_class:
	class_destroy(aim_class);
free_cdev:
	unregister_chrdev_region(aim_devno, 1);
	return -EIO;
}

static void __exit mod_exit(void)
{
	struct aim_channel *channel, *tmp;

	pr_info("exit module\n");

	most_deregister_aim(&cdev_aim);

	list_for_each_entry_safe(channel, tmp, &channel_list, list) {
		device_destroy(aim_class, channel->devno);
		cdev_del(&channel->cdev);
		kfifo_free(&channel->fifo);
		list_del(&channel->list);
		ida_simple_remove(&minor_id, MINOR(channel->devno));
		kfree(channel);
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
