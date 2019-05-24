/*
 * Userspace driver for the LED subsystem
 *
 * Copyright (C) 2016 David Lechner <david@lechnology.com>
 *
 * Based on uinput.c: Aristeu Sergio Rozanski Filho <aris@cathedrallabs.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/leds.h>
#include <linux/miscdevice.h>
#include <linux/module.h>
#include <linux/poll.h>
#include <linux/sched.h>
#include <linux/slab.h>

#include <uapi/linux/uleds.h>

#define ULEDS_NAME	"uleds"

enum uleds_state {
	ULEDS_STATE_UNKNOWN,
	ULEDS_STATE_REGISTERED,
};

struct uleds_device {
	struct uleds_user_dev	user_dev;
	struct led_classdev	led_cdev;
	struct mutex		mutex;
	enum uleds_state	state;
	wait_queue_head_t	waitq;
	int			brightness;
	bool			new_data;
};

static struct miscdevice uleds_misc;

static void uleds_brightness_set(struct led_classdev *led_cdev,
				 enum led_brightness brightness)
{
	struct uleds_device *udev = container_of(led_cdev, struct uleds_device,
						 led_cdev);

	if (udev->brightness != brightness) {
		udev->brightness = brightness;
		udev->new_data = true;
		wake_up_interruptible(&udev->waitq);
	}
}

static int uleds_open(struct inode *inode, struct file *file)
{
	struct uleds_device *udev;

	udev = kzalloc(sizeof(*udev), GFP_KERNEL);
	if (!udev)
		return -ENOMEM;

	udev->led_cdev.name = udev->user_dev.name;
	udev->led_cdev.brightness_set = uleds_brightness_set;

	mutex_init(&udev->mutex);
	init_waitqueue_head(&udev->waitq);
	udev->state = ULEDS_STATE_UNKNOWN;

	file->private_data = udev;
	stream_open(inode, file);

	return 0;
}

static ssize_t uleds_write(struct file *file, const char __user *buffer,
			   size_t count, loff_t *ppos)
{
	struct uleds_device *udev = file->private_data;
	const char *name;
	int ret;

	if (count == 0)
		return 0;

	ret = mutex_lock_interruptible(&udev->mutex);
	if (ret)
		return ret;

	if (udev->state == ULEDS_STATE_REGISTERED) {
		ret = -EBUSY;
		goto out;
	}

	if (count != sizeof(struct uleds_user_dev)) {
		ret = -EINVAL;
		goto out;
	}

	if (copy_from_user(&udev->user_dev, buffer,
			   sizeof(struct uleds_user_dev))) {
		ret = -EFAULT;
		goto out;
	}

	name = udev->user_dev.name;
	if (!name[0] || !strcmp(name, ".") || !strcmp(name, "..") ||
	    strchr(name, '/')) {
		ret = -EINVAL;
		goto out;
	}

	if (udev->user_dev.max_brightness <= 0) {
		ret = -EINVAL;
		goto out;
	}
	udev->led_cdev.max_brightness = udev->user_dev.max_brightness;

	ret = devm_led_classdev_register(uleds_misc.this_device,
					 &udev->led_cdev);
	if (ret < 0)
		goto out;

	udev->new_data = true;
	udev->state = ULEDS_STATE_REGISTERED;
	ret = count;

out:
	mutex_unlock(&udev->mutex);

	return ret;
}

static ssize_t uleds_read(struct file *file, char __user *buffer, size_t count,
			  loff_t *ppos)
{
	struct uleds_device *udev = file->private_data;
	ssize_t retval;

	if (count < sizeof(udev->brightness))
		return 0;

	do {
		retval = mutex_lock_interruptible(&udev->mutex);
		if (retval)
			return retval;

		if (udev->state != ULEDS_STATE_REGISTERED) {
			retval = -ENODEV;
		} else if (!udev->new_data && (file->f_flags & O_NONBLOCK)) {
			retval = -EAGAIN;
		} else if (udev->new_data) {
			retval = copy_to_user(buffer, &udev->brightness,
					      sizeof(udev->brightness));
			udev->new_data = false;
			retval = sizeof(udev->brightness);
		}

		mutex_unlock(&udev->mutex);

		if (retval)
			break;

		if (!(file->f_flags & O_NONBLOCK))
			retval = wait_event_interruptible(udev->waitq,
					udev->new_data ||
					udev->state != ULEDS_STATE_REGISTERED);
	} while (retval == 0);

	return retval;
}

static __poll_t uleds_poll(struct file *file, poll_table *wait)
{
	struct uleds_device *udev = file->private_data;

	poll_wait(file, &udev->waitq, wait);

	if (udev->new_data)
		return EPOLLIN | EPOLLRDNORM;

	return 0;
}

static int uleds_release(struct inode *inode, struct file *file)
{
	struct uleds_device *udev = file->private_data;

	if (udev->state == ULEDS_STATE_REGISTERED) {
		udev->state = ULEDS_STATE_UNKNOWN;
		devm_led_classdev_unregister(uleds_misc.this_device,
					     &udev->led_cdev);
	}
	kfree(udev);

	return 0;
}

static const struct file_operations uleds_fops = {
	.owner		= THIS_MODULE,
	.open		= uleds_open,
	.release	= uleds_release,
	.read		= uleds_read,
	.write		= uleds_write,
	.poll		= uleds_poll,
	.llseek		= no_llseek,
};

static struct miscdevice uleds_misc = {
	.fops		= &uleds_fops,
	.minor		= MISC_DYNAMIC_MINOR,
	.name		= ULEDS_NAME,
};

static int __init uleds_init(void)
{
	return misc_register(&uleds_misc);
}
module_init(uleds_init);

static void __exit uleds_exit(void)
{
	misc_deregister(&uleds_misc);
}
module_exit(uleds_exit);

MODULE_AUTHOR("David Lechner <david@lechnology.com>");
MODULE_DESCRIPTION("Userspace driver for the LED subsystem");
MODULE_LICENSE("GPL");
