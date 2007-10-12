/*
 * $Id: tsdev.c,v 1.15 2002/04/10 16:50:19 jsimmons Exp $
 *
 *  Copyright (c) 2001 "Crazy" james Simmons
 *
 *  Compaq touchscreen protocol driver. The protocol emulated by this driver
 *  is obsolete; for new programs use the tslib library which can read directly
 *  from evdev and perform dejittering, variance filtering and calibration -
 *  all in user space, not at kernel level. The meaning of this driver is
 *  to allow usage of newer input drivers with old applications that use the
 *  old /dev/h3600_ts and /dev/h3600_tsraw devices.
 *
 *  09-Apr-2004: Andrew Zabolotny <zap@homelink.ru>
 *      Fixed to actually work, not just output random numbers.
 *      Added support for both h3600_ts and h3600_tsraw protocol
 *      emulation.
 */

/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 *
 * Should you need to contact me, the author, you can do so either by
 * e-mail - mail your message to <jsimmons@infradead.org>.
 */

#define TSDEV_MINOR_BASE	128
#define TSDEV_MINORS		32
/* First 16 devices are h3600_ts compatible; second 16 are h3600_tsraw */
#define TSDEV_MINOR_MASK	15
#define TSDEV_BUFFER_SIZE	64

#include <linux/slab.h>
#include <linux/poll.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/init.h>
#include <linux/input.h>
#include <linux/major.h>
#include <linux/random.h>
#include <linux/time.h>
#include <linux/device.h>

#ifndef CONFIG_INPUT_TSDEV_SCREEN_X
#define CONFIG_INPUT_TSDEV_SCREEN_X	240
#endif
#ifndef CONFIG_INPUT_TSDEV_SCREEN_Y
#define CONFIG_INPUT_TSDEV_SCREEN_Y	320
#endif

/* This driver emulates both protocols of the old h3600_ts and h3600_tsraw
 * devices. The first one must output X/Y data in 'cooked' format, e.g.
 * filtered, dejittered and calibrated. Second device just outputs raw
 * data received from the hardware.
 *
 * This driver doesn't support filtering and dejittering; it supports only
 * calibration. Filtering and dejittering must be done in the low-level
 * driver, if needed, because it may gain additional benefits from knowing
 * the low-level details, the nature of noise and so on.
 *
 * The driver precomputes a calibration matrix given the initial xres and
 * yres values (quite innacurate for most touchscreens) that will result
 * in a more or less expected range of output values. The driver supports
 * the TS_SET_CAL ioctl, which will replace the calibration matrix with a
 * new one, supposedly generated from the values taken from the raw device.
 */

MODULE_AUTHOR("James Simmons <jsimmons@transvirtual.com>");
MODULE_DESCRIPTION("Input driver to touchscreen converter");
MODULE_LICENSE("GPL");

static int xres = CONFIG_INPUT_TSDEV_SCREEN_X;
module_param(xres, uint, 0);
MODULE_PARM_DESC(xres, "Horizontal screen resolution (can be negative for X-mirror)");

static int yres = CONFIG_INPUT_TSDEV_SCREEN_Y;
module_param(yres, uint, 0);
MODULE_PARM_DESC(yres, "Vertical screen resolution (can be negative for Y-mirror)");

/* From Compaq's Touch Screen Specification version 0.2 (draft) */
struct ts_event {
	short pressure;
	short x;
	short y;
	short millisecs;
};

struct ts_calibration {
	int xscale;
	int xtrans;
	int yscale;
	int ytrans;
	int xyswap;
};

struct tsdev {
	int exist;
	int open;
	int minor;
	char name[8];
	struct input_handle handle;
	wait_queue_head_t wait;
	struct list_head client_list;
	spinlock_t client_lock; /* protects client_list */
	struct mutex mutex;
	struct device dev;

	int x, y, pressure;
	struct ts_calibration cal;
};

struct tsdev_client {
	struct fasync_struct *fasync;
	struct list_head node;
	struct tsdev *tsdev;
	struct ts_event buffer[TSDEV_BUFFER_SIZE];
	int head, tail;
	spinlock_t buffer_lock; /* protects access to buffer, head and tail */
	int raw;
};

/* The following ioctl codes are defined ONLY for backward compatibility.
 * Don't use tsdev for new developement; use the tslib library instead.
 * Touchscreen calibration is a fully userspace task.
 */
/* Use 'f' as magic number */
#define IOC_H3600_TS_MAGIC  'f'
#define TS_GET_CAL	_IOR(IOC_H3600_TS_MAGIC, 10, struct ts_calibration)
#define TS_SET_CAL	_IOW(IOC_H3600_TS_MAGIC, 11, struct ts_calibration)

static struct tsdev *tsdev_table[TSDEV_MINORS/2];
static DEFINE_MUTEX(tsdev_table_mutex);

static int tsdev_fasync(int fd, struct file *file, int on)
{
	struct tsdev_client *client = file->private_data;
	int retval;

	retval = fasync_helper(fd, file, on, &client->fasync);

	return retval < 0 ? retval : 0;
}

static void tsdev_free(struct device *dev)
{
	struct tsdev *tsdev = container_of(dev, struct tsdev, dev);

	kfree(tsdev);
}

static void tsdev_attach_client(struct tsdev *tsdev, struct tsdev_client *client)
{
	spin_lock(&tsdev->client_lock);
	list_add_tail_rcu(&client->node, &tsdev->client_list);
	spin_unlock(&tsdev->client_lock);
	synchronize_sched();
}

static void tsdev_detach_client(struct tsdev *tsdev, struct tsdev_client *client)
{
	spin_lock(&tsdev->client_lock);
	list_del_rcu(&client->node);
	spin_unlock(&tsdev->client_lock);
	synchronize_sched();
}

static int tsdev_open_device(struct tsdev *tsdev)
{
	int retval;

	retval = mutex_lock_interruptible(&tsdev->mutex);
	if (retval)
		return retval;

	if (!tsdev->exist)
		retval = -ENODEV;
	else if (!tsdev->open++) {
		retval = input_open_device(&tsdev->handle);
		if (retval)
			tsdev->open--;
	}

	mutex_unlock(&tsdev->mutex);
	return retval;
}

static void tsdev_close_device(struct tsdev *tsdev)
{
	mutex_lock(&tsdev->mutex);

	if (tsdev->exist && !--tsdev->open)
		input_close_device(&tsdev->handle);

	mutex_unlock(&tsdev->mutex);
}

/*
 * Wake up users waiting for IO so they can disconnect from
 * dead device.
 */
static void tsdev_hangup(struct tsdev *tsdev)
{
	struct tsdev_client *client;

	spin_lock(&tsdev->client_lock);
	list_for_each_entry(client, &tsdev->client_list, node)
		kill_fasync(&client->fasync, SIGIO, POLL_HUP);
	spin_unlock(&tsdev->client_lock);

	wake_up_interruptible(&tsdev->wait);
}

static int tsdev_release(struct inode *inode, struct file *file)
{
	struct tsdev_client *client = file->private_data;
	struct tsdev *tsdev = client->tsdev;

	tsdev_fasync(-1, file, 0);
	tsdev_detach_client(tsdev, client);
	kfree(client);

	tsdev_close_device(tsdev);
	put_device(&tsdev->dev);

	return 0;
}

static int tsdev_open(struct inode *inode, struct file *file)
{
	int i = iminor(inode) - TSDEV_MINOR_BASE;
	struct tsdev_client *client;
	struct tsdev *tsdev;
	int error;

	printk(KERN_WARNING "tsdev (compaq touchscreen emulation) is scheduled "
		"for removal.\nSee Documentation/feature-removal-schedule.txt "
		"for details.\n");

	if (i >= TSDEV_MINORS)
		return -ENODEV;

	error = mutex_lock_interruptible(&tsdev_table_mutex);
	if (error)
		return error;
	tsdev = tsdev_table[i & TSDEV_MINOR_MASK];
	if (tsdev)
		get_device(&tsdev->dev);
	mutex_unlock(&tsdev_table_mutex);

	if (!tsdev)
		return -ENODEV;

	client = kzalloc(sizeof(struct tsdev_client), GFP_KERNEL);
	if (!client) {
		error = -ENOMEM;
		goto err_put_tsdev;
	}

	spin_lock_init(&client->buffer_lock);
	client->tsdev = tsdev;
	client->raw = i >= TSDEV_MINORS / 2;
	tsdev_attach_client(tsdev, client);

	error = tsdev_open_device(tsdev);
	if (error)
		goto err_free_client;

	file->private_data = client;
	return 0;

 err_free_client:
	tsdev_detach_client(tsdev, client);
	kfree(client);
 err_put_tsdev:
	put_device(&tsdev->dev);
	return error;
}

static int tsdev_fetch_next_event(struct tsdev_client *client,
				  struct ts_event *event)
{
	int have_event;

	spin_lock_irq(&client->buffer_lock);

	have_event = client->head != client->tail;
	if (have_event) {
		*event = client->buffer[client->tail++];
		client->tail &= TSDEV_BUFFER_SIZE - 1;
	}

	spin_unlock_irq(&client->buffer_lock);

	return have_event;
}

static ssize_t tsdev_read(struct file *file, char __user *buffer, size_t count,
			  loff_t *ppos)
{
	struct tsdev_client *client = file->private_data;
	struct tsdev *tsdev = client->tsdev;
	struct ts_event event;
	int retval;

	if (client->head == client->tail && tsdev->exist &&
	    (file->f_flags & O_NONBLOCK))
		return -EAGAIN;

	retval = wait_event_interruptible(tsdev->wait,
			client->head != client->tail || !tsdev->exist);
	if (retval)
		return retval;

	if (!tsdev->exist)
		return -ENODEV;

	while (retval + sizeof(struct ts_event) <= count &&
	       tsdev_fetch_next_event(client, &event)) {

		if (copy_to_user(buffer + retval, &event,
				 sizeof(struct ts_event)))
			return -EFAULT;

		retval += sizeof(struct ts_event);
	}

	return retval;
}

/* No kernel lock - fine */
static unsigned int tsdev_poll(struct file *file, poll_table *wait)
{
	struct tsdev_client *client = file->private_data;
	struct tsdev *tsdev = client->tsdev;

	poll_wait(file, &tsdev->wait, wait);
	return ((client->head == client->tail) ? 0 : (POLLIN | POLLRDNORM)) |
		(tsdev->exist ? 0 : (POLLHUP | POLLERR));
}

static long tsdev_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	struct tsdev_client *client = file->private_data;
	struct tsdev *tsdev = client->tsdev;
	int retval = 0;

	retval = mutex_lock_interruptible(&tsdev->mutex);
	if (retval)
		return retval;

	if (!tsdev->exist) {
		retval = -ENODEV;
		goto out;
	}

	switch (cmd) {

	case TS_GET_CAL:
		if (copy_to_user((void __user *)arg, &tsdev->cal,
				 sizeof (struct ts_calibration)))
			retval = -EFAULT;
		break;

	case TS_SET_CAL:
		if (copy_from_user(&tsdev->cal, (void __user *)arg,
				   sizeof(struct ts_calibration)))
			retval = -EFAULT;
		break;

	default:
		retval = -EINVAL;
		break;
	}

 out:
	mutex_unlock(&tsdev->mutex);
	return retval;
}

static const struct file_operations tsdev_fops = {
	.owner		= THIS_MODULE,
	.open		= tsdev_open,
	.release	= tsdev_release,
	.read		= tsdev_read,
	.poll		= tsdev_poll,
	.fasync		= tsdev_fasync,
	.unlocked_ioctl	= tsdev_ioctl,
};

static void tsdev_pass_event(struct tsdev *tsdev, struct tsdev_client *client,
			     int x, int y, int pressure, int millisecs)
{
	struct ts_event *event;
	int tmp;

	/* Interrupts are already disabled, just acquire the lock */
	spin_lock(&client->buffer_lock);

	event = &client->buffer[client->head++];
	client->head &= TSDEV_BUFFER_SIZE - 1;

	/* Calibration */
	if (!client->raw) {
		x = ((x * tsdev->cal.xscale) >> 8) + tsdev->cal.xtrans;
		y = ((y * tsdev->cal.yscale) >> 8) + tsdev->cal.ytrans;
		if (tsdev->cal.xyswap) {
			tmp = x; x = y; y = tmp;
		}
	}

	event->millisecs = millisecs;
	event->x = x;
	event->y = y;
	event->pressure = pressure;

	spin_unlock(&client->buffer_lock);

	kill_fasync(&client->fasync, SIGIO, POLL_IN);
}

static void tsdev_distribute_event(struct tsdev *tsdev)
{
	struct tsdev_client *client;
	struct timeval time;
	int millisecs;

	do_gettimeofday(&time);
	millisecs = time.tv_usec / 1000;

	list_for_each_entry_rcu(client, &tsdev->client_list, node)
		tsdev_pass_event(tsdev, client,
				 tsdev->x, tsdev->y,
				 tsdev->pressure, millisecs);
}

static void tsdev_event(struct input_handle *handle, unsigned int type,
			unsigned int code, int value)
{
	struct tsdev *tsdev = handle->private;
	struct input_dev *dev = handle->dev;
	int wake_up_readers = 0;

	switch (type) {

	case EV_ABS:
		switch (code) {

		case ABS_X:
			tsdev->x = value;
			break;

		case ABS_Y:
			tsdev->y = value;
			break;

		case ABS_PRESSURE:
			if (value > dev->absmax[ABS_PRESSURE])
				value = dev->absmax[ABS_PRESSURE];
			value -= dev->absmin[ABS_PRESSURE];
			if (value < 0)
				value = 0;
			tsdev->pressure = value;
			break;
		}
		break;

	case EV_REL:
		switch (code) {

		case REL_X:
			tsdev->x += value;
			if (tsdev->x < 0)
				tsdev->x = 0;
			else if (tsdev->x > xres)
				tsdev->x = xres;
			break;

		case REL_Y:
			tsdev->y += value;
			if (tsdev->y < 0)
				tsdev->y = 0;
			else if (tsdev->y > yres)
				tsdev->y = yres;
			break;
		}
		break;

	case EV_KEY:
		if (code == BTN_TOUCH || code == BTN_MOUSE) {
			switch (value) {

			case 0:
				tsdev->pressure = 0;
				break;

			case 1:
				if (!tsdev->pressure)
					tsdev->pressure = 1;
				break;
			}
		}
		break;

	case EV_SYN:
		if (code == SYN_REPORT) {
			tsdev_distribute_event(tsdev);
			wake_up_readers = 1;
		}
		break;
	}

	if (wake_up_readers)
		wake_up_interruptible(&tsdev->wait);
}

static int tsdev_install_chrdev(struct tsdev *tsdev)
{
	tsdev_table[tsdev->minor] = tsdev;
	return 0;
}

static void tsdev_remove_chrdev(struct tsdev *tsdev)
{
	mutex_lock(&tsdev_table_mutex);
	tsdev_table[tsdev->minor] = NULL;
	mutex_unlock(&tsdev_table_mutex);
}

/*
 * Mark device non-existant. This disables writes, ioctls and
 * prevents new users from opening the device. Already posted
 * blocking reads will stay, however new ones will fail.
 */
static void tsdev_mark_dead(struct tsdev *tsdev)
{
	mutex_lock(&tsdev->mutex);
	tsdev->exist = 0;
	mutex_unlock(&tsdev->mutex);
}

static void tsdev_cleanup(struct tsdev *tsdev)
{
	struct input_handle *handle = &tsdev->handle;

	tsdev_mark_dead(tsdev);
	tsdev_hangup(tsdev);
	tsdev_remove_chrdev(tsdev);

	/* tsdev is marked dead so noone else accesses tsdev->open */
	if (tsdev->open)
		input_close_device(handle);
}

static int tsdev_connect(struct input_handler *handler, struct input_dev *dev,
			 const struct input_device_id *id)
{
	struct tsdev *tsdev;
	int delta;
	int minor;
	int error;

	for (minor = 0; minor < TSDEV_MINORS / 2; minor++)
		if (!tsdev_table[minor])
			break;

	if (minor == TSDEV_MINORS) {
		printk(KERN_ERR "tsdev: no more free tsdev devices\n");
		return -ENFILE;
	}

	tsdev = kzalloc(sizeof(struct tsdev), GFP_KERNEL);
	if (!tsdev)
		return -ENOMEM;

	INIT_LIST_HEAD(&tsdev->client_list);
	spin_lock_init(&tsdev->client_lock);
	mutex_init(&tsdev->mutex);
	init_waitqueue_head(&tsdev->wait);

	snprintf(tsdev->name, sizeof(tsdev->name), "ts%d", minor);
	tsdev->exist = 1;
	tsdev->minor = minor;

	tsdev->handle.dev = dev;
	tsdev->handle.name = tsdev->name;
	tsdev->handle.handler = handler;
	tsdev->handle.private = tsdev;

	/* Precompute the rough calibration matrix */
	delta = dev->absmax [ABS_X] - dev->absmin [ABS_X] + 1;
	if (delta == 0)
		delta = 1;
	tsdev->cal.xscale = (xres << 8) / delta;
	tsdev->cal.xtrans = - ((dev->absmin [ABS_X] * tsdev->cal.xscale) >> 8);

	delta = dev->absmax [ABS_Y] - dev->absmin [ABS_Y] + 1;
	if (delta == 0)
		delta = 1;
	tsdev->cal.yscale = (yres << 8) / delta;
	tsdev->cal.ytrans = - ((dev->absmin [ABS_Y] * tsdev->cal.yscale) >> 8);

	strlcpy(tsdev->dev.bus_id, tsdev->name, sizeof(tsdev->dev.bus_id));
	tsdev->dev.devt = MKDEV(INPUT_MAJOR, TSDEV_MINOR_BASE + minor);
	tsdev->dev.class = &input_class;
	tsdev->dev.parent = &dev->dev;
	tsdev->dev.release = tsdev_free;
	device_initialize(&tsdev->dev);

	error = input_register_handle(&tsdev->handle);
	if (error)
		goto err_free_tsdev;

	error = tsdev_install_chrdev(tsdev);
	if (error)
		goto err_unregister_handle;

	error = device_add(&tsdev->dev);
	if (error)
		goto err_cleanup_tsdev;

	return 0;

 err_cleanup_tsdev:
	tsdev_cleanup(tsdev);
 err_unregister_handle:
	input_unregister_handle(&tsdev->handle);
 err_free_tsdev:
	put_device(&tsdev->dev);
	return error;
}

static void tsdev_disconnect(struct input_handle *handle)
{
	struct tsdev *tsdev = handle->private;

	device_del(&tsdev->dev);
	tsdev_cleanup(tsdev);
	input_unregister_handle(handle);
	put_device(&tsdev->dev);
}

static const struct input_device_id tsdev_ids[] = {
	{
	      .flags	= INPUT_DEVICE_ID_MATCH_EVBIT | INPUT_DEVICE_ID_MATCH_KEYBIT | INPUT_DEVICE_ID_MATCH_RELBIT,
	      .evbit	= { BIT(EV_KEY) | BIT(EV_REL) },
	      .keybit	= { [LONG(BTN_LEFT)] = BIT(BTN_LEFT) },
	      .relbit	= { BIT(REL_X) | BIT(REL_Y) },
	}, /* A mouse like device, at least one button, two relative axes */

	{
	      .flags	= INPUT_DEVICE_ID_MATCH_EVBIT | INPUT_DEVICE_ID_MATCH_KEYBIT | INPUT_DEVICE_ID_MATCH_ABSBIT,
	      .evbit	= { BIT(EV_KEY) | BIT(EV_ABS) },
	      .keybit	= { [LONG(BTN_TOUCH)] = BIT(BTN_TOUCH) },
	      .absbit	= { BIT(ABS_X) | BIT(ABS_Y) },
	}, /* A tablet like device, at least touch detection, two absolute axes */

	{
	      .flags	= INPUT_DEVICE_ID_MATCH_EVBIT | INPUT_DEVICE_ID_MATCH_ABSBIT,
	      .evbit	= { BIT(EV_ABS) },
	      .absbit	= { BIT(ABS_X) | BIT(ABS_Y) | BIT(ABS_PRESSURE) },
	}, /* A tablet like device with several gradations of pressure */

	{} /* Terminating entry */
};

MODULE_DEVICE_TABLE(input, tsdev_ids);

static struct input_handler tsdev_handler = {
	.event		= tsdev_event,
	.connect	= tsdev_connect,
	.disconnect	= tsdev_disconnect,
	.fops		= &tsdev_fops,
	.minor		= TSDEV_MINOR_BASE,
	.name		= "tsdev",
	.id_table	= tsdev_ids,
};

static int __init tsdev_init(void)
{
	return input_register_handler(&tsdev_handler);
}

static void __exit tsdev_exit(void)
{
	input_unregister_handler(&tsdev_handler);
}

module_init(tsdev_init);
module_exit(tsdev_exit);
