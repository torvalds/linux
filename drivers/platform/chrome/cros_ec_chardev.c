// SPDX-License-Identifier: GPL-2.0
/*
 * Miscellaneous character driver for ChromeOS Embedded Controller
 *
 * Copyright 2014 Google, Inc.
 * Copyright 2019 Google LLC
 *
 * This file is a rework and part of the code is ported from
 * drivers/mfd/cros_ec_dev.c that was originally written by
 * Bill Richardson.
 */

#include <linux/init.h>
#include <linux/device.h>
#include <linux/fs.h>
#include <linux/mfd/cros_ec.h>
#include <linux/mfd/cros_ec_commands.h>
#include <linux/miscdevice.h>
#include <linux/module.h>
#include <linux/platform_data/cros_ec_chardev.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/types.h>
#include <linux/uaccess.h>

#define DRV_NAME		"cros-ec-chardev"

struct chardev_data {
	struct cros_ec_dev *ec_dev;
	struct miscdevice misc;
};

static int ec_get_version(struct cros_ec_dev *ec, char *str, int maxlen)
{
	static const char * const current_image_name[] = {
		"unknown", "read-only", "read-write", "invalid",
	};
	struct ec_response_get_version *resp;
	struct cros_ec_command *msg;
	int ret;

	msg = kzalloc(sizeof(*msg) + sizeof(*resp), GFP_KERNEL);
	if (!msg)
		return -ENOMEM;

	msg->command = EC_CMD_GET_VERSION + ec->cmd_offset;
	msg->insize = sizeof(*resp);

	ret = cros_ec_cmd_xfer_status(ec->ec_dev, msg);
	if (ret < 0) {
		snprintf(str, maxlen,
			 "Unknown EC version, returned error: %d\n",
			 msg->result);
		goto exit;
	}

	resp = (struct ec_response_get_version *)msg->data;
	if (resp->current_image >= ARRAY_SIZE(current_image_name))
		resp->current_image = 3; /* invalid */

	snprintf(str, maxlen, "%s\n%s\n%s\n%s\n", CROS_EC_DEV_VERSION,
		 resp->version_string_ro, resp->version_string_rw,
		 current_image_name[resp->current_image]);

	ret = 0;
exit:
	kfree(msg);
	return ret;
}

/*
 * Device file ops
 */
static int cros_ec_chardev_open(struct inode *inode, struct file *filp)
{
	struct miscdevice *mdev = filp->private_data;
	struct cros_ec_dev *ec_dev = dev_get_drvdata(mdev->parent);

	filp->private_data = ec_dev;
	nonseekable_open(inode, filp);

	return 0;
}

static ssize_t cros_ec_chardev_read(struct file *filp, char __user *buffer,
				     size_t length, loff_t *offset)
{
	char msg[sizeof(struct ec_response_get_version) +
		 sizeof(CROS_EC_DEV_VERSION)];
	struct cros_ec_dev *ec = filp->private_data;
	size_t count;
	int ret;

	if (*offset != 0)
		return 0;

	ret = ec_get_version(ec, msg, sizeof(msg));
	if (ret)
		return ret;

	count = min(length, strlen(msg));

	if (copy_to_user(buffer, msg, count))
		return -EFAULT;

	*offset = count;
	return count;
}

/*
 * Ioctls
 */
static long cros_ec_chardev_ioctl_xcmd(struct cros_ec_dev *ec, void __user *arg)
{
	struct cros_ec_command *s_cmd;
	struct cros_ec_command u_cmd;
	long ret;

	if (copy_from_user(&u_cmd, arg, sizeof(u_cmd)))
		return -EFAULT;

	if (u_cmd.outsize > EC_MAX_MSG_BYTES ||
	    u_cmd.insize > EC_MAX_MSG_BYTES)
		return -EINVAL;

	s_cmd = kmalloc(sizeof(*s_cmd) + max(u_cmd.outsize, u_cmd.insize),
			GFP_KERNEL);
	if (!s_cmd)
		return -ENOMEM;

	if (copy_from_user(s_cmd, arg, sizeof(*s_cmd) + u_cmd.outsize)) {
		ret = -EFAULT;
		goto exit;
	}

	if (u_cmd.outsize != s_cmd->outsize ||
	    u_cmd.insize != s_cmd->insize) {
		ret = -EINVAL;
		goto exit;
	}

	s_cmd->command += ec->cmd_offset;
	ret = cros_ec_cmd_xfer(ec->ec_dev, s_cmd);
	/* Only copy data to userland if data was received. */
	if (ret < 0)
		goto exit;

	if (copy_to_user(arg, s_cmd, sizeof(*s_cmd) + s_cmd->insize))
		ret = -EFAULT;
exit:
	kfree(s_cmd);
	return ret;
}

static long cros_ec_chardev_ioctl_readmem(struct cros_ec_dev *ec,
					   void __user *arg)
{
	struct cros_ec_device *ec_dev = ec->ec_dev;
	struct cros_ec_readmem s_mem = { };
	long num;

	/* Not every platform supports direct reads */
	if (!ec_dev->cmd_readmem)
		return -ENOTTY;

	if (copy_from_user(&s_mem, arg, sizeof(s_mem)))
		return -EFAULT;

	num = ec_dev->cmd_readmem(ec_dev, s_mem.offset, s_mem.bytes,
				  s_mem.buffer);
	if (num <= 0)
		return num;

	if (copy_to_user((void __user *)arg, &s_mem, sizeof(s_mem)))
		return -EFAULT;

	return num;
}

static long cros_ec_chardev_ioctl(struct file *filp, unsigned int cmd,
				   unsigned long arg)
{
	struct cros_ec_dev *ec = filp->private_data;

	if (_IOC_TYPE(cmd) != CROS_EC_DEV_IOC)
		return -ENOTTY;

	switch (cmd) {
	case CROS_EC_DEV_IOCXCMD:
		return cros_ec_chardev_ioctl_xcmd(ec, (void __user *)arg);
	case CROS_EC_DEV_IOCRDMEM:
		return cros_ec_chardev_ioctl_readmem(ec, (void __user *)arg);
	}

	return -ENOTTY;
}

static const struct file_operations chardev_fops = {
	.open		= cros_ec_chardev_open,
	.read		= cros_ec_chardev_read,
	.unlocked_ioctl	= cros_ec_chardev_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl	= cros_ec_chardev_ioctl,
#endif
};

static int cros_ec_chardev_probe(struct platform_device *pdev)
{
	struct cros_ec_dev *ec_dev = dev_get_drvdata(pdev->dev.parent);
	struct cros_ec_platform *ec_platform = dev_get_platdata(ec_dev->dev);
	struct chardev_data *data;

	/* Create a char device: we want to create it anew */
	data = devm_kzalloc(&pdev->dev, sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	data->ec_dev = ec_dev;
	data->misc.minor = MISC_DYNAMIC_MINOR;
	data->misc.fops = &chardev_fops;
	data->misc.name = ec_platform->ec_name;
	data->misc.parent = pdev->dev.parent;

	dev_set_drvdata(&pdev->dev, data);

	return misc_register(&data->misc);
}

static int cros_ec_chardev_remove(struct platform_device *pdev)
{
	struct chardev_data *data = dev_get_drvdata(&pdev->dev);

	misc_deregister(&data->misc);

	return 0;
}

static struct platform_driver cros_ec_chardev_driver = {
	.driver = {
		.name = DRV_NAME,
	},
	.probe = cros_ec_chardev_probe,
	.remove = cros_ec_chardev_remove,
};

module_platform_driver(cros_ec_chardev_driver);

MODULE_ALIAS("platform:" DRV_NAME);
MODULE_AUTHOR("Enric Balletbo i Serra <enric.balletbo@collabora.com>");
MODULE_DESCRIPTION("ChromeOS EC Miscellaneous Character Driver");
MODULE_LICENSE("GPL");
