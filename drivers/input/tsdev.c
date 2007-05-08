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
	wait_queue_head_t wait;
	struct list_head client_list;
	struct input_handle handle;
	int x, y, pressure;
	struct ts_calibration cal;
};

struct tsdev_client {
	struct fasync_struct *fasync;
	struct list_head node;
	struct tsdev *tsdev;
	int head, tail;
	struct ts_event event[TSDEV_BUFFER_SIZE];
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

static int tsdev_fasync(int fd, struct file *file, int on)
{
	struct tsdev_client *client = file->private_data;
	int retval;

	retval = fasync_helper(fd, file, on, &client->fasync);
	return retval < 0 ? retval : 0;
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

	tsdev = tsdev_table[i & TSDEV_MINOR_MASK];
	if (!tsdev || !tsdev->exist)
		return -ENODEV;

	client = kzalloc(sizeof(struct tsdev_client), GFP_KERNEL);
	if (!client)
		return -ENOMEM;

	client->tsdev = tsdev;
	client->raw = (i >= TSDEV_MINORS / 2) ? 1 : 0;
	list_add_tail(&client->node, &tsdev->client_list);

	if (!tsdev->open++ && tsdev->exist) {
		error = input_open_device(&tsdev->handle);
		if (error) {
			list_del(&client->node);
			kfree(client);
			return error;
		}
	}

	file->private_data = client;
	return 0;
}

static void tsdev_free(struct tsdev *tsdev)
{
	tsdev_table[tsdev->minor] = NULL;
	kfree(tsdev);
}

static int tsdev_release(struct inode *inode, struct file *file)
{
	struct tsdev_client *client = file->private_data;
	struct tsdev *tsdev = client->tsdev;

	tsdev_fasync(-1, file, 0);

	list_del(&client->node);
	kfree(client);

	if (!--tsdev->open) {
		if (tsdev->exist)
			input_close_device(&tsdev->handle);
		else
			tsdev_free(tsdev);
	}

	return 0;
}

static ssize_t tsdev_read(struct file *file, char __user *buffer, size_t count,
			  loff_t *ppos)
{
	struct tsdev_client *client = file->private_data;
	struct tsdev *tsdev = client->tsdev;
	int retval = 0;

	if (client->head == client->tail && tsdev->exist && (file->f_flags & O_NONBLOCK))
		return -EAGAIN;

	retval = wait_event_interruptible(tsdev->wait,
			client->head != client->tail || !tsdev->exist);
	if (retval)
		return retval;

	if (!tsdev->exist)
		return -ENODEV;

	while (client->head != client->tail &&
	       retval + sizeof (struct ts_event) <= count) {
		if (copy_to_user (buffer + retval, client->event + client->tail,
				  sizeof (struct ts_event)))
			return -EFAULT;
		client->tail = (client->tail + 1) & (TSDEV_BUFFER_SIZE - 1);
		retval += sizeof (struct ts_event);
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

static int tsdev_ioctl(struct inode *inode, struct file *file,
		       unsigned int cmd, unsigned long arg)
{
	struct tsdev_client *client = file->private_data;
	struct tsdev *tsdev = client->tsdev;
	int retval = 0;

	switch (cmd) {
	case TS_GET_CAL:
		if (copy_to_user((void __user *)arg, &tsdev->cal,
				 sizeof (struct ts_calibration)))
			retval = -EFAULT;
		break;

	case TS_SET_CAL:
		if (copy_from_user(&tsdev->cal, (void __user *)arg,
				   sizeof (struct ts_calibration)))
			retval = -EFAULT;
		break;

	default:
		retval = -EINVAL;
		break;
	}

	return retval;
}

static const struct file_operations tsdev_fops = {
	.owner =	THIS_MODULE,
	.open =		tsdev_open,
	.release =	tsdev_release,
	.read =		tsdev_read,
	.poll =		tsdev_poll,
	.fasync =	tsdev_fasync,
	.ioctl =	tsdev_ioctl,
};

static void tsdev_event(struct input_handle *handle, unsigned int type,
			unsigned int code, int value)
{
	struct tsdev *tsdev = handle->private;
	struct tsdev_client *client;
	struct timeval time;

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
			if (value > handle->dev->absmax[ABS_PRESSURE])
				value = handle->dev->absmax[ABS_PRESSURE];
			value -= handle->dev->absmin[ABS_PRESSURE];
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
	}

	if (type != EV_SYN || code != SYN_REPORT)
		return;

	list_for_each_entry(client, &tsdev->client_list, node) {
		int x, y, tmp;

		do_gettimeofday(&time);
		client->event[client->head].millisecs = time.tv_usec / 100;
		client->event[client->head].pressure = tsdev->pressure;

		x = tsdev->x;
		y = tsdev->y;

		/* Calibration */
		if (!client->raw) {
			x = ((x * tsdev->cal.xscale) >> 8) + tsdev->cal.xtrans;
			y = ((y * tsdev->cal.yscale) >> 8) + tsdev->cal.ytrans;
			if (tsdev->cal.xyswap) {
				tmp = x; x = y; y = tmp;
			}
		}

		client->event[client->head].x = x;
		client->event[client->head].y = y;
		client->head = (client->head + 1) & (TSDEV_BUFFER_SIZE - 1);
		kill_fasync(&client->fasync, SIGIO, POLL_IN);
	}
	wake_up_interruptible(&tsdev->wait);
}

static int tsdev_connect(struct input_handler *handler, struct input_dev *dev,
			 const struct input_device_id *id)
{
	struct tsdev *tsdev;
	struct class_device *cdev;
	dev_t devt;
	int minor, delta;
	int error;

	for (minor = 0; minor < TSDEV_MINORS / 2 && tsdev_table[minor]; minor++);
	if (minor >= TSDEV_MINORS / 2) {
		printk(KERN_ERR
		       "tsdev: You have way too many touchscreens\n");
		return -ENFILE;
	}

	tsdev = kzalloc(sizeof(struct tsdev), GFP_KERNEL);
	if (!tsdev)
		return -ENOMEM;

	INIT_LIST_HEAD(&tsdev->client_list);
	init_waitqueue_head(&tsdev->wait);

	sprintf(tsdev->name, "ts%d", minor);

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

	tsdev_table[minor] = tsdev;

	devt = MKDEV(INPUT_MAJOR, TSDEV_MINOR_BASE + minor),

	cdev = class_device_create(&input_class, &dev->cdev, devt,
				   dev->cdev.dev, tsdev->name);
	if (IS_ERR(cdev)) {
		error = PTR_ERR(cdev);
		goto err_free_tsdev;
	}

	/* temporary symlink to keep userspace happy */
	error = sysfs_create_link(&input_class.subsys.kobj,
				  &cdev->kobj, tsdev->name);
	if (error)
		goto err_cdev_destroy;

	error = input_register_handle(&tsdev->handle);
	if (error)
		goto err_remove_link;

	return 0;

 err_remove_link:
	sysfs_remove_link(&input_class.subsys.kobj, tsdev->name);
 err_cdev_destroy:
	class_device_destroy(&input_class, devt);
 err_free_tsdev:
	tsdev_table[minor] = NULL;
	kfree(tsdev);
	return error;
}

static void tsdev_disconnect(struct input_handle *handle)
{
	struct tsdev *tsdev = handle->private;
	struct tsdev_client *client;

	input_unregister_handle(handle);

	sysfs_remove_link(&input_class.subsys.kobj, tsdev->name);
	class_device_destroy(&input_class,
			MKDEV(INPUT_MAJOR, TSDEV_MINOR_BASE + tsdev->minor));
	tsdev->exist = 0;

	if (tsdev->open) {
		input_close_device(handle);
		wake_up_interruptible(&tsdev->wait);
		list_for_each_entry(client, &tsdev->client_list, node)
			kill_fasync(&client->fasync, SIGIO, POLL_HUP);
	} else
		tsdev_free(tsdev);
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
	.event =	tsdev_event,
	.connect =	tsdev_connect,
	.disconnect =	tsdev_disconnect,
	.fops =		&tsdev_fops,
	.minor =	TSDEV_MINOR_BASE,
	.name =		"tsdev",
	.id_table =	tsdev_ids,
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
