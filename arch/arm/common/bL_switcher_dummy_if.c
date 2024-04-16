// SPDX-License-Identifier: GPL-2.0-only
/*
 * arch/arm/common/bL_switcher_dummy_if.c -- b.L switcher dummy interface
 *
 * Created by:	Nicolas Pitre, November 2012
 * Copyright:	(C) 2012-2013  Linaro Limited
 *
 * Dummy interface to user space for debugging purpose only.
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/miscdevice.h>
#include <linux/uaccess.h>
#include <asm/bL_switcher.h>

static ssize_t bL_switcher_write(struct file *file, const char __user *buf,
			size_t len, loff_t *pos)
{
	unsigned char val[3];
	unsigned int cpu, cluster;
	int ret;

	pr_debug("%s\n", __func__);

	if (len < 3)
		return -EINVAL;

	if (copy_from_user(val, buf, 3))
		return -EFAULT;

	/* format: <cpu#>,<cluster#> */
	if (val[0] < '0' || val[0] > '9' ||
	    val[1] != ',' ||
	    val[2] < '0' || val[2] > '1')
		return -EINVAL;

	cpu = val[0] - '0';
	cluster = val[2] - '0';
	ret = bL_switch_request(cpu, cluster);

	return ret ? : len;
}

static const struct file_operations bL_switcher_fops = {
	.write		= bL_switcher_write,
	.owner	= THIS_MODULE,
};

static struct miscdevice bL_switcher_device = {
	MISC_DYNAMIC_MINOR,
	"b.L_switcher",
	&bL_switcher_fops
};
module_misc_device(bL_switcher_device);

MODULE_AUTHOR("Nicolas Pitre <nico@linaro.org>");
MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("big.LITTLE switcher dummy user interface");
