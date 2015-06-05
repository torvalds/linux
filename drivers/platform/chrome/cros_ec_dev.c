/*
 * cros_ec_dev - expose the Chrome OS Embedded Controller to user-space
 *
 * Copyright (C) 2014 Google, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#include <linux/fs.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/uaccess.h>

#include "cros_ec_dev.h"

/* Device variables */
#define CROS_MAX_DEV 128
static struct class *cros_class;
static int ec_major;

/* Basic communication */
static int ec_get_version(struct cros_ec_device *ec, char *str, int maxlen)
{
	struct ec_response_get_version *resp;
	static const char * const current_image_name[] = {
		"unknown", "read-only", "read-write", "invalid",
	};
	struct cros_ec_command msg = {
		.version = 0,
		.command = EC_CMD_GET_VERSION,
		.outdata = { 0 },
		.outsize = 0,
		.indata = { 0 },
		.insize = sizeof(*resp),
	};
	int ret;

	ret = cros_ec_cmd_xfer(ec, &msg);
	if (ret < 0)
		return ret;

	if (msg.result != EC_RES_SUCCESS) {
		snprintf(str, maxlen,
			 "%s\nUnknown EC version: EC returned %d\n",
			 CROS_EC_DEV_VERSION, msg.result);
		return 0;
	}

	resp = (struct ec_response_get_version *)msg.indata;
	if (resp->current_image >= ARRAY_SIZE(current_image_name))
		resp->current_image = 3; /* invalid */

	snprintf(str, maxlen, "%s\n%s\n%s\n%s\n", CROS_EC_DEV_VERSION,
		 resp->version_string_ro, resp->version_string_rw,
		 current_image_name[resp->current_image]);

	return 0;
}

/* Device file ops */
static int ec_device_open(struct inode *inode, struct file *filp)
{
	filp->private_data = container_of(inode->i_cdev,
					  struct cros_ec_device, cdev);
	return 0;
}

static int ec_device_release(struct inode *inode, struct file *filp)
{
	return 0;
}

static ssize_t ec_device_read(struct file *filp, char __user *buffer,
			      size_t length, loff_t *offset)
{
	struct cros_ec_device *ec = filp->private_data;
	char msg[sizeof(struct ec_response_get_version) +
		 sizeof(CROS_EC_DEV_VERSION)];
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

/* Ioctls */
static long ec_device_ioctl_xcmd(struct cros_ec_device *ec, void __user *arg)
{
	long ret;
	struct cros_ec_command s_cmd = { };

	if (copy_from_user(&s_cmd, arg, sizeof(s_cmd)))
		return -EFAULT;

	ret = cros_ec_cmd_xfer(ec, &s_cmd);
	/* Only copy data to userland if data was received. */
	if (ret < 0)
		return ret;

	if (copy_to_user(arg, &s_cmd, sizeof(s_cmd)))
		return -EFAULT;

	return 0;
}

static long ec_device_ioctl_readmem(struct cros_ec_device *ec, void __user *arg)
{
	struct cros_ec_readmem s_mem = { };
	long num;

	/* Not every platform supports direct reads */
	if (!ec->cmd_readmem)
		return -ENOTTY;

	if (copy_from_user(&s_mem, arg, sizeof(s_mem)))
		return -EFAULT;

	num = ec->cmd_readmem(ec, s_mem.offset, s_mem.bytes, s_mem.buffer);
	if (num <= 0)
		return num;

	if (copy_to_user((void __user *)arg, &s_mem, sizeof(s_mem)))
		return -EFAULT;

	return 0;
}

static long ec_device_ioctl(struct file *filp, unsigned int cmd,
			    unsigned long arg)
{
	struct cros_ec_device *ec = filp->private_data;

	if (_IOC_TYPE(cmd) != CROS_EC_DEV_IOC)
		return -ENOTTY;

	switch (cmd) {
	case CROS_EC_DEV_IOCXCMD:
		return ec_device_ioctl_xcmd(ec, (void __user *)arg);
	case CROS_EC_DEV_IOCRDMEM:
		return ec_device_ioctl_readmem(ec, (void __user *)arg);
	}

	return -ENOTTY;
}

/* Module initialization */
static const struct file_operations fops = {
	.open = ec_device_open,
	.release = ec_device_release,
	.read = ec_device_read,
	.unlocked_ioctl = ec_device_ioctl,
};

static int ec_device_probe(struct platform_device *pdev)
{
	struct cros_ec_device *ec = dev_get_drvdata(pdev->dev.parent);
	int retval = -ENOTTY;
	dev_t devno = MKDEV(ec_major, 0);

	/* Instantiate it (and remember the EC) */
	cdev_init(&ec->cdev, &fops);

	retval = cdev_add(&ec->cdev, devno, 1);
	if (retval) {
		dev_err(&pdev->dev, ": failed to add character device\n");
		return retval;
	}

	ec->vdev = device_create(cros_class, NULL, devno, ec,
				 CROS_EC_DEV_NAME);
	if (IS_ERR(ec->vdev)) {
		retval = PTR_ERR(ec->vdev);
		dev_err(&pdev->dev, ": failed to create device\n");
		cdev_del(&ec->cdev);
		return retval;
	}

	/* Initialize extra interfaces */
	ec_dev_sysfs_init(ec);
	ec_dev_lightbar_init(ec);

	return 0;
}

static int ec_device_remove(struct platform_device *pdev)
{
	struct cros_ec_device *ec = dev_get_drvdata(pdev->dev.parent);

	ec_dev_lightbar_remove(ec);
	ec_dev_sysfs_remove(ec);
	device_destroy(cros_class, MKDEV(ec_major, 0));
	cdev_del(&ec->cdev);
	return 0;
}

static struct platform_driver cros_ec_dev_driver = {
	.driver = {
		.name = "cros-ec-ctl",
	},
	.probe = ec_device_probe,
	.remove = ec_device_remove,
};

static int __init cros_ec_dev_init(void)
{
	int ret;
	dev_t dev = 0;

	cros_class = class_create(THIS_MODULE, "chromeos");
	if (IS_ERR(cros_class)) {
		pr_err(CROS_EC_DEV_NAME ": failed to register device class\n");
		return PTR_ERR(cros_class);
	}

	/* Get a range of minor numbers (starting with 0) to work with */
	ret = alloc_chrdev_region(&dev, 0, CROS_MAX_DEV, CROS_EC_DEV_NAME);
	if (ret < 0) {
		pr_err(CROS_EC_DEV_NAME ": alloc_chrdev_region() failed\n");
		goto failed_chrdevreg;
	}
	ec_major = MAJOR(dev);

	/* Register the driver */
	ret = platform_driver_register(&cros_ec_dev_driver);
	if (ret < 0) {
		pr_warn(CROS_EC_DEV_NAME ": can't register driver: %d\n", ret);
		goto failed_devreg;
	}
	return 0;

failed_devreg:
	unregister_chrdev_region(MKDEV(ec_major, 0), CROS_MAX_DEV);
failed_chrdevreg:
	class_destroy(cros_class);
	return ret;
}

static void __exit cros_ec_dev_exit(void)
{
	platform_driver_unregister(&cros_ec_dev_driver);
	unregister_chrdev(ec_major, CROS_EC_DEV_NAME);
	class_destroy(cros_class);
}

module_init(cros_ec_dev_init);
module_exit(cros_ec_dev_exit);

MODULE_AUTHOR("Bill Richardson <wfrichar@chromium.org>");
MODULE_DESCRIPTION("Userspace interface to the Chrome OS Embedded Controller");
MODULE_VERSION("1.0");
MODULE_LICENSE("GPL");
