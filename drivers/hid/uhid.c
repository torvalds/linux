/*
 * User-space I/O driver support for HID subsystem
 * Copyright (c) 2012 David Herrmann
 */

/*
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 */

#include <linux/atomic.h>
#include <linux/device.h>
#include <linux/fs.h>
#include <linux/hid.h>
#include <linux/input.h>
#include <linux/miscdevice.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/poll.h>
#include <linux/sched.h>
#include <linux/spinlock.h>
#include <linux/uhid.h>
#include <linux/wait.h>

#define UHID_NAME	"uhid"

static struct miscdevice uhid_misc;

static int uhid_char_open(struct inode *inode, struct file *file)
{
	return 0;
}

static int uhid_char_release(struct inode *inode, struct file *file)
{
	return 0;
}

static ssize_t uhid_char_read(struct file *file, char __user *buffer,
				size_t count, loff_t *ppos)
{
	return 0;
}

static ssize_t uhid_char_write(struct file *file, const char __user *buffer,
				size_t count, loff_t *ppos)
{
	return 0;
}

static unsigned int uhid_char_poll(struct file *file, poll_table *wait)
{
	return 0;
}

static const struct file_operations uhid_fops = {
	.owner		= THIS_MODULE,
	.open		= uhid_char_open,
	.release	= uhid_char_release,
	.read		= uhid_char_read,
	.write		= uhid_char_write,
	.poll		= uhid_char_poll,
	.llseek		= no_llseek,
};

static struct miscdevice uhid_misc = {
	.fops		= &uhid_fops,
	.minor		= MISC_DYNAMIC_MINOR,
	.name		= UHID_NAME,
};

static int __init uhid_init(void)
{
	return misc_register(&uhid_misc);
}

static void __exit uhid_exit(void)
{
	misc_deregister(&uhid_misc);
}

module_init(uhid_init);
module_exit(uhid_exit);
MODULE_LICENSE("GPL");
MODULE_AUTHOR("David Herrmann <dh.herrmann@gmail.com>");
MODULE_DESCRIPTION("User-space I/O driver support for HID subsystem");
