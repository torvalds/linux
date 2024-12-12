// SPDX-License-Identifier: GPL-2.0
/*
 * Provide diagnose information via misc device /dev/diag.
 *
 * Copyright IBM Corp. 2024
 */

#include <linux/fs.h>
#include <linux/init.h>
#include <linux/ioctl.h>
#include <linux/kernel.h>
#include <linux/miscdevice.h>
#include <linux/types.h>

static long diag_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
	long rc;

	switch (cmd) {
	default:
		rc = -ENOIOCTLCMD;
		break;
	}
	return rc;
}

static const struct file_operations fops = {
	.owner		= THIS_MODULE,
	.open		= nonseekable_open,
	.unlocked_ioctl	= diag_ioctl,
};

static struct miscdevice diagdev = {
	.name	= "diag",
	.minor	= MISC_DYNAMIC_MINOR,
	.fops	= &fops,
	.mode	= 0444,
};

static int diag_init(void)
{
	return misc_register(&diagdev);
}

device_initcall(diag_init);
