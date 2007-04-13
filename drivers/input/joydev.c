/*
 * Joystick device driver for the input driver suite.
 *
 * Copyright (c) 1999-2002 Vojtech Pavlik
 * Copyright (c) 1999 Colin Van Dyke
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <asm/io.h>
#include <asm/system.h>
#include <linux/delay.h>
#include <linux/errno.h>
#include <linux/joystick.h>
#include <linux/input.h>
#include <linux/kernel.h>
#include <linux/major.h>
#include <linux/slab.h>
#include <linux/mm.h>
#include <linux/miscdevice.h>
#include <linux/module.h>
#include <linux/poll.h>
#include <linux/init.h>
#include <linux/smp_lock.h>
#include <linux/device.h>

MODULE_AUTHOR("Vojtech Pavlik <vojtech@ucw.cz>");
MODULE_DESCRIPTION("Joystick device interfaces");
MODULE_SUPPORTED_DEVICE("input/js");
MODULE_LICENSE("GPL");

#define JOYDEV_MINOR_BASE	0
#define JOYDEV_MINORS		16
#define JOYDEV_BUFFER_SIZE	64

struct joydev {
	int exist;
	int open;
	int minor;
	char name[16];
	struct input_handle handle;
	wait_queue_head_t wait;
	struct list_head list;
	struct js_corr corr[ABS_MAX + 1];
	struct JS_DATA_SAVE_TYPE glue;
	int nabs;
	int nkey;
	__u16 keymap[KEY_MAX - BTN_MISC + 1];
	__u16 keypam[KEY_MAX - BTN_MISC + 1];
	__u8 absmap[ABS_MAX + 1];
	__u8 abspam[ABS_MAX + 1];
	__s16 abs[ABS_MAX + 1];
};

struct joydev_list {
	struct js_event buffer[JOYDEV_BUFFER_SIZE];
	int head;
	int tail;
	int startup;
	struct fasync_struct *fasync;
	struct joydev *joydev;
	struct list_head node;
};

static struct joydev *joydev_table[JOYDEV_MINORS];

static int joydev_correct(int value, struct js_corr *corr)
{
	switch (corr->type) {
		case JS_CORR_NONE:
			break;
		case JS_CORR_BROKEN:
			value = value > corr->coef[0] ? (value < corr->coef[1] ? 0 :
				((corr->coef[3] * (value - corr->coef[1])) >> 14)) :
				((corr->coef[2] * (value - corr->coef[0])) >> 14);
			break;
		default:
			return 0;
	}

	return value < -32767 ? -32767 : (value > 32767 ? 32767 : value);
}

static void joydev_event(struct input_handle *handle, unsigned int type, unsigned int code, int value)
{
	struct joydev *joydev = handle->private;
	struct joydev_list *list;
	struct js_event event;

	switch (type) {

		case EV_KEY:
			if (code < BTN_MISC || value == 2)
				return;
			event.type = JS_EVENT_BUTTON;
			event.number = joydev->keymap[code - BTN_MISC];
			event.value = value;
			break;

		case EV_ABS:
			event.type = JS_EVENT_AXIS;
			event.number = joydev->absmap[code];
			event.value = joydev_correct(value, joydev->corr + event.number);
			if (event.value == joydev->abs[event.number])
				return;
			joydev->abs[event.number] = event.value;
			break;

		default:
			return;
	}

	event.time = jiffies_to_msecs(jiffies);

	list_for_each_entry(list, &joydev->list, node) {

		memcpy(list->buffer + list->head, &event, sizeof(struct js_event));

		if (list->startup == joydev->nabs + joydev->nkey)
			if (list->tail == (list->head = (list->head + 1) & (JOYDEV_BUFFER_SIZE - 1)))
				list->startup = 0;

		kill_fasync(&list->fasync, SIGIO, POLL_IN);
	}

	wake_up_interruptible(&joydev->wait);
}

static int joydev_fasync(int fd, struct file *file, int on)
{
	int retval;
	struct joydev_list *list = file->private_data;

	retval = fasync_helper(fd, file, on, &list->fasync);

	return retval < 0 ? retval : 0;
}

static void joydev_free(struct joydev *joydev)
{
	joydev_table[joydev->minor] = NULL;
	kfree(joydev);
}

static int joydev_release(struct inode * inode, struct file * file)
{
	struct joydev_list *list = file->private_data;

	joydev_fasync(-1, file, 0);

	list_del(&list->node);

	if (!--list->joydev->open) {
		if (list->joydev->exist)
			input_close_device(&list->joydev->handle);
		else
			joydev_free(list->joydev);
	}

	kfree(list);
	return 0;
}

static int joydev_open(struct inode *inode, struct file *file)
{
	struct joydev_list *list;
	int i = iminor(inode) - JOYDEV_MINOR_BASE;

	if (i >= JOYDEV_MINORS || !joydev_table[i])
		return -ENODEV;

	if (!(list = kzalloc(sizeof(struct joydev_list), GFP_KERNEL)))
		return -ENOMEM;

	list->joydev = joydev_table[i];
	list_add_tail(&list->node, &joydev_table[i]->list);
	file->private_data = list;

	if (!list->joydev->open++)
		if (list->joydev->exist)
			input_open_device(&list->joydev->handle);

	return 0;
}

static ssize_t joydev_write(struct file * file, const char __user * buffer, size_t count, loff_t *ppos)
{
	return -EINVAL;
}

static ssize_t joydev_read(struct file *file, char __user *buf, size_t count, loff_t *ppos)
{
	struct joydev_list *list = file->private_data;
	struct joydev *joydev = list->joydev;
	struct input_dev *input = joydev->handle.dev;
	int retval = 0;

	if (!list->joydev->exist)
		return -ENODEV;

	if (count < sizeof(struct js_event))
		return -EINVAL;

	if (count == sizeof(struct JS_DATA_TYPE)) {

		struct JS_DATA_TYPE data;
		int i;

		for (data.buttons = i = 0; i < 32 && i < joydev->nkey; i++)
			data.buttons |= test_bit(joydev->keypam[i], input->key) ? (1 << i) : 0;
		data.x = (joydev->abs[0] / 256 + 128) >> joydev->glue.JS_CORR.x;
		data.y = (joydev->abs[1] / 256 + 128) >> joydev->glue.JS_CORR.y;

		if (copy_to_user(buf, &data, sizeof(struct JS_DATA_TYPE)))
			return -EFAULT;

		list->startup = 0;
		list->tail = list->head;

		return sizeof(struct JS_DATA_TYPE);
	}

	if (list->startup == joydev->nabs + joydev->nkey &&
	    list->head == list->tail && (file->f_flags & O_NONBLOCK))
		return -EAGAIN;

	retval = wait_event_interruptible(list->joydev->wait,
					  !list->joydev->exist ||
					  list->startup < joydev->nabs + joydev->nkey ||
					  list->head != list->tail);

	if (retval)
		return retval;

	if (!list->joydev->exist)
		return -ENODEV;

	while (list->startup < joydev->nabs + joydev->nkey && retval + sizeof(struct js_event) <= count) {

		struct js_event event;

		event.time = jiffies_to_msecs(jiffies);

		if (list->startup < joydev->nkey) {
			event.type = JS_EVENT_BUTTON | JS_EVENT_INIT;
			event.number = list->startup;
			event.value = !!test_bit(joydev->keypam[event.number], input->key);
		} else {
			event.type = JS_EVENT_AXIS | JS_EVENT_INIT;
			event.number = list->startup - joydev->nkey;
			event.value = joydev->abs[event.number];
		}

		if (copy_to_user(buf + retval, &event, sizeof(struct js_event)))
			return -EFAULT;

		list->startup++;
		retval += sizeof(struct js_event);
	}

	while (list->head != list->tail && retval + sizeof(struct js_event) <= count) {

		if (copy_to_user(buf + retval, list->buffer + list->tail, sizeof(struct js_event)))
			return -EFAULT;

		list->tail = (list->tail + 1) & (JOYDEV_BUFFER_SIZE - 1);
		retval += sizeof(struct js_event);
	}

	return retval;
}

/* No kernel lock - fine */
static unsigned int joydev_poll(struct file *file, poll_table *wait)
{
	struct joydev_list *list = file->private_data;

	poll_wait(file, &list->joydev->wait, wait);
	return ((list->head != list->tail || list->startup < list->joydev->nabs + list->joydev->nkey) ?
		(POLLIN | POLLRDNORM) : 0) | (list->joydev->exist ? 0 : (POLLHUP | POLLERR));
}

static int joydev_ioctl_common(struct joydev *joydev, unsigned int cmd, void __user *argp)
{
	struct input_dev *dev = joydev->handle.dev;
	int i, j;

	switch (cmd) {

		case JS_SET_CAL:
			return copy_from_user(&joydev->glue.JS_CORR, argp,
				sizeof(joydev->glue.JS_CORR)) ? -EFAULT : 0;

		case JS_GET_CAL:
			return copy_to_user(argp, &joydev->glue.JS_CORR,
				sizeof(joydev->glue.JS_CORR)) ? -EFAULT : 0;

		case JS_SET_TIMEOUT:
			return get_user(joydev->glue.JS_TIMEOUT, (s32 __user *) argp);

		case JS_GET_TIMEOUT:
			return put_user(joydev->glue.JS_TIMEOUT, (s32 __user *) argp);

		case JSIOCGVERSION:
			return put_user(JS_VERSION, (__u32 __user *) argp);

		case JSIOCGAXES:
			return put_user(joydev->nabs, (__u8 __user *) argp);

		case JSIOCGBUTTONS:
			return put_user(joydev->nkey, (__u8 __user *) argp);

		case JSIOCSCORR:
			if (copy_from_user(joydev->corr, argp,
				      sizeof(joydev->corr[0]) * joydev->nabs))
			    return -EFAULT;
			for (i = 0; i < joydev->nabs; i++) {
				j = joydev->abspam[i];
			        joydev->abs[i] = joydev_correct(dev->abs[j], joydev->corr + i);
			}
			return 0;

		case JSIOCGCORR:
			return copy_to_user(argp, joydev->corr,
						sizeof(joydev->corr[0]) * joydev->nabs) ? -EFAULT : 0;

		case JSIOCSAXMAP:
			if (copy_from_user(joydev->abspam, argp, sizeof(__u8) * (ABS_MAX + 1)))
				return -EFAULT;
			for (i = 0; i < joydev->nabs; i++) {
				if (joydev->abspam[i] > ABS_MAX)
					return -EINVAL;
				joydev->absmap[joydev->abspam[i]] = i;
			}
			return 0;

		case JSIOCGAXMAP:
			return copy_to_user(argp, joydev->abspam,
						sizeof(__u8) * (ABS_MAX + 1)) ? -EFAULT : 0;

		case JSIOCSBTNMAP:
			if (copy_from_user(joydev->keypam, argp, sizeof(__u16) * (KEY_MAX - BTN_MISC + 1)))
				return -EFAULT;
			for (i = 0; i < joydev->nkey; i++) {
				if (joydev->keypam[i] > KEY_MAX || joydev->keypam[i] < BTN_MISC)
					return -EINVAL;
				joydev->keymap[joydev->keypam[i] - BTN_MISC] = i;
			}
			return 0;

		case JSIOCGBTNMAP:
			return copy_to_user(argp, joydev->keypam,
						sizeof(__u16) * (KEY_MAX - BTN_MISC + 1)) ? -EFAULT : 0;

		default:
			if ((cmd & ~(_IOC_SIZEMASK << _IOC_SIZESHIFT)) == JSIOCGNAME(0)) {
				int len;
				if (!dev->name)
					return 0;
				len = strlen(dev->name) + 1;
				if (len > _IOC_SIZE(cmd))
					len = _IOC_SIZE(cmd);
				if (copy_to_user(argp, dev->name, len))
					return -EFAULT;
				return len;
			}
	}
	return -EINVAL;
}

#ifdef CONFIG_COMPAT
static long joydev_compat_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	struct joydev_list *list = file->private_data;
	struct joydev *joydev = list->joydev;
	void __user *argp = (void __user *)arg;
	s32 tmp32;
	struct JS_DATA_SAVE_TYPE_32 ds32;
	int err;

	if (!joydev->exist)
		return -ENODEV;

	switch(cmd) {
	case JS_SET_TIMELIMIT:
		err = get_user(tmp32, (s32 __user *) arg);
		if (err == 0)
			joydev->glue.JS_TIMELIMIT = tmp32;
		break;
	case JS_GET_TIMELIMIT:
		tmp32 = joydev->glue.JS_TIMELIMIT;
		err = put_user(tmp32, (s32 __user *) arg);
		break;

	case JS_SET_ALL:
		err = copy_from_user(&ds32, argp,
				     sizeof(ds32)) ? -EFAULT : 0;
		if (err == 0) {
			joydev->glue.JS_TIMEOUT    = ds32.JS_TIMEOUT;
			joydev->glue.BUSY          = ds32.BUSY;
			joydev->glue.JS_EXPIRETIME = ds32.JS_EXPIRETIME;
			joydev->glue.JS_TIMELIMIT  = ds32.JS_TIMELIMIT;
			joydev->glue.JS_SAVE       = ds32.JS_SAVE;
			joydev->glue.JS_CORR       = ds32.JS_CORR;
		}
		break;

	case JS_GET_ALL:
		ds32.JS_TIMEOUT    = joydev->glue.JS_TIMEOUT;
		ds32.BUSY          = joydev->glue.BUSY;
		ds32.JS_EXPIRETIME = joydev->glue.JS_EXPIRETIME;
		ds32.JS_TIMELIMIT  = joydev->glue.JS_TIMELIMIT;
		ds32.JS_SAVE       = joydev->glue.JS_SAVE;
		ds32.JS_CORR       = joydev->glue.JS_CORR;

		err = copy_to_user(argp, &ds32, sizeof(ds32)) ? -EFAULT : 0;
		break;

	default:
		err = joydev_ioctl_common(joydev, cmd, argp);
	}
	return err;
}
#endif /* CONFIG_COMPAT */

static int joydev_ioctl(struct inode *inode, struct file *file, unsigned int cmd, unsigned long arg)
{
	struct joydev_list *list = file->private_data;
	struct joydev *joydev = list->joydev;
	void __user *argp = (void __user *)arg;

	if (!joydev->exist)
		return -ENODEV;

	switch(cmd) {
		case JS_SET_TIMELIMIT:
			return get_user(joydev->glue.JS_TIMELIMIT, (long __user *) arg);
		case JS_GET_TIMELIMIT:
			return put_user(joydev->glue.JS_TIMELIMIT, (long __user *) arg);
		case JS_SET_ALL:
			return copy_from_user(&joydev->glue, argp,
						sizeof(joydev->glue)) ? -EFAULT : 0;
		case JS_GET_ALL:
			return copy_to_user(argp, &joydev->glue,
						sizeof(joydev->glue)) ? -EFAULT : 0;
		default:
			return joydev_ioctl_common(joydev, cmd, argp);
	}
}

static const struct file_operations joydev_fops = {
	.owner =	THIS_MODULE,
	.read =		joydev_read,
	.write =	joydev_write,
	.poll =		joydev_poll,
	.open =		joydev_open,
	.release =	joydev_release,
	.ioctl =	joydev_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl =	joydev_compat_ioctl,
#endif
	.fasync =	joydev_fasync,
};

static struct input_handle *joydev_connect(struct input_handler *handler, struct input_dev *dev,
					   const struct input_device_id *id)
{
	struct joydev *joydev;
	struct class_device *cdev;
	int i, j, t, minor;

	for (minor = 0; minor < JOYDEV_MINORS && joydev_table[minor]; minor++);
	if (minor == JOYDEV_MINORS) {
		printk(KERN_ERR "joydev: no more free joydev devices\n");
		return NULL;
	}

	if (!(joydev = kzalloc(sizeof(struct joydev), GFP_KERNEL)))
		return NULL;

	INIT_LIST_HEAD(&joydev->list);
	init_waitqueue_head(&joydev->wait);

	joydev->minor = minor;
	joydev->exist = 1;
	joydev->handle.dev = dev;
	joydev->handle.name = joydev->name;
	joydev->handle.handler = handler;
	joydev->handle.private = joydev;
	sprintf(joydev->name, "js%d", minor);

	for (i = 0; i < ABS_MAX + 1; i++)
		if (test_bit(i, dev->absbit)) {
			joydev->absmap[i] = joydev->nabs;
			joydev->abspam[joydev->nabs] = i;
			joydev->nabs++;
		}

	for (i = BTN_JOYSTICK - BTN_MISC; i < KEY_MAX - BTN_MISC + 1; i++)
		if (test_bit(i + BTN_MISC, dev->keybit)) {
			joydev->keymap[i] = joydev->nkey;
			joydev->keypam[joydev->nkey] = i + BTN_MISC;
			joydev->nkey++;
		}

	for (i = 0; i < BTN_JOYSTICK - BTN_MISC; i++)
		if (test_bit(i + BTN_MISC, dev->keybit)) {
			joydev->keymap[i] = joydev->nkey;
			joydev->keypam[joydev->nkey] = i + BTN_MISC;
			joydev->nkey++;
		}

	for (i = 0; i < joydev->nabs; i++) {
		j = joydev->abspam[i];
		if (dev->absmax[j] == dev->absmin[j]) {
			joydev->corr[i].type = JS_CORR_NONE;
			joydev->abs[i] = dev->abs[j];
			continue;
		}
		joydev->corr[i].type = JS_CORR_BROKEN;
		joydev->corr[i].prec = dev->absfuzz[j];
		joydev->corr[i].coef[0] = (dev->absmax[j] + dev->absmin[j]) / 2 - dev->absflat[j];
		joydev->corr[i].coef[1] = (dev->absmax[j] + dev->absmin[j]) / 2 + dev->absflat[j];
		if (!(t = ((dev->absmax[j] - dev->absmin[j]) / 2 - 2 * dev->absflat[j])))
			continue;
		joydev->corr[i].coef[2] = (1 << 29) / t;
		joydev->corr[i].coef[3] = (1 << 29) / t;

		joydev->abs[i] = joydev_correct(dev->abs[j], joydev->corr + i);
	}

	joydev_table[minor] = joydev;

	cdev = class_device_create(&input_class, &dev->cdev,
			MKDEV(INPUT_MAJOR, JOYDEV_MINOR_BASE + minor),
			dev->cdev.dev, joydev->name);

	/* temporary symlink to keep userspace happy */
	sysfs_create_link(&input_class.subsys.kobj, &cdev->kobj,
			  joydev->name);

	return &joydev->handle;
}

static void joydev_disconnect(struct input_handle *handle)
{
	struct joydev *joydev = handle->private;
	struct joydev_list *list;

	sysfs_remove_link(&input_class.subsys.kobj, joydev->name);
	class_device_destroy(&input_class, MKDEV(INPUT_MAJOR, JOYDEV_MINOR_BASE + joydev->minor));
	joydev->exist = 0;

	if (joydev->open) {
		input_close_device(handle);
		wake_up_interruptible(&joydev->wait);
		list_for_each_entry(list, &joydev->list, node)
			kill_fasync(&list->fasync, SIGIO, POLL_HUP);
	} else
		joydev_free(joydev);
}

static const struct input_device_id joydev_blacklist[] = {
	{
		.flags = INPUT_DEVICE_ID_MATCH_EVBIT | INPUT_DEVICE_ID_MATCH_KEYBIT,
		.evbit = { BIT(EV_KEY) },
		.keybit = { [LONG(BTN_TOUCH)] = BIT(BTN_TOUCH) },
	},	/* Avoid itouchpads, touchscreens and tablets */
	{ }	/* Terminating entry */
};

static const struct input_device_id joydev_ids[] = {
	{
		.flags = INPUT_DEVICE_ID_MATCH_EVBIT | INPUT_DEVICE_ID_MATCH_ABSBIT,
		.evbit = { BIT(EV_ABS) },
		.absbit = { BIT(ABS_X) },
	},
	{
		.flags = INPUT_DEVICE_ID_MATCH_EVBIT | INPUT_DEVICE_ID_MATCH_ABSBIT,
		.evbit = { BIT(EV_ABS) },
		.absbit = { BIT(ABS_WHEEL) },
	},
	{
		.flags = INPUT_DEVICE_ID_MATCH_EVBIT | INPUT_DEVICE_ID_MATCH_ABSBIT,
		.evbit = { BIT(EV_ABS) },
		.absbit = { BIT(ABS_THROTTLE) },
	},
	{ }	/* Terminating entry */
};

MODULE_DEVICE_TABLE(input, joydev_ids);

static struct input_handler joydev_handler = {
	.event =	joydev_event,
	.connect =	joydev_connect,
	.disconnect =	joydev_disconnect,
	.fops =		&joydev_fops,
	.minor =	JOYDEV_MINOR_BASE,
	.name =		"joydev",
	.id_table =	joydev_ids,
	.blacklist =	joydev_blacklist,
};

static int __init joydev_init(void)
{
	return input_register_handler(&joydev_handler);
}

static void __exit joydev_exit(void)
{
	input_unregister_handler(&joydev_handler);
}

module_init(joydev_init);
module_exit(joydev_exit);
