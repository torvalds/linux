// SPDX-License-Identifier: GPL-2.0-only
/*
 * ntsync.c - Kernel driver for NT synchronization primitives
 *
 * Copyright (C) 2024 Elizabeth Figura <zfigura@codeweavers.com>
 */

#include <linux/fs.h>
#include <linux/miscdevice.h>
#include <linux/module.h>

#define NTSYNC_NAME	"ntsync"

static int ntsync_char_open(struct inode *inode, struct file *file)
{
	return nonseekable_open(inode, file);
}

static int ntsync_char_release(struct inode *inode, struct file *file)
{
	return 0;
}

static long ntsync_char_ioctl(struct file *file, unsigned int cmd,
			      unsigned long parm)
{
	switch (cmd) {
	default:
		return -ENOIOCTLCMD;
	}
}

static const struct file_operations ntsync_fops = {
	.owner		= THIS_MODULE,
	.open		= ntsync_char_open,
	.release	= ntsync_char_release,
	.unlocked_ioctl	= ntsync_char_ioctl,
	.compat_ioctl	= compat_ptr_ioctl,
	.llseek		= no_llseek,
};

static struct miscdevice ntsync_misc = {
	.minor		= MISC_DYNAMIC_MINOR,
	.name		= NTSYNC_NAME,
	.fops		= &ntsync_fops,
};

module_misc_device(ntsync_misc);

MODULE_AUTHOR("Elizabeth Figura <zfigura@codeweavers.com>");
MODULE_DESCRIPTION("Kernel driver for NT synchronization primitives");
MODULE_LICENSE("GPL");
