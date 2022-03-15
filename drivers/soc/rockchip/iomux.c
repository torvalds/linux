// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2022 Rockchip Electronics Co. Ltd.
 */

#include <linux/device.h>
#include <linux/err.h>
#include <linux/file.h>
#include <linux/fs.h>
#include <linux/list.h>
#include <linux/uaccess.h>
#include <linux/ioctl.h>
#include <linux/types.h>
#include <linux/miscdevice.h>
#include <linux/slab.h>
#include <uapi/linux/rk-iomux.h>
#include "../../pinctrl/pinctrl-rockchip.h"

struct rk_iomux_device {
	struct miscdevice dev;
};

static long rk_iomux_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	struct iomux_ioctl_data data;
	int ret = 0;

	if (_IOC_SIZE(cmd) > sizeof(data))
		return -EINVAL;

	if (copy_from_user(&data, (void __user *)arg, _IOC_SIZE(cmd)))
		return -EFAULT;

	if (!(_IOC_DIR(cmd) & _IOC_WRITE))
		memset(&data, 0, sizeof(data));

	switch (cmd) {
	case IOMUX_IOC_MUX_SET:
		ret = rk_iomux_set(data.bank, data.pin, data.mux);
		if (ret)
			return ret;
		break;
	case IOMUX_IOC_MUX_GET:
		ret = rk_iomux_get(data.bank, data.pin, &data.mux);
		if (ret)
			return ret;
		break;
	default:
		return -ENOTTY;
	}

	if (_IOC_DIR(cmd) & _IOC_READ) {
		if (copy_to_user((void __user *)arg, &data, _IOC_SIZE(cmd)))
			return -EFAULT;
	}

	return ret;
}

static const struct file_operations rk_iomux_fops = {
	.owner          = THIS_MODULE,
	.unlocked_ioctl = rk_iomux_ioctl,
	.compat_ioctl	= compat_ptr_ioctl,
};

static __init int rk_iomux_device_create(void)
{
	struct rk_iomux_device *cdev;
	int ret;

	cdev = kzalloc(sizeof(*cdev), GFP_KERNEL);
	if (!cdev)
		return -ENOMEM;

	cdev->dev.minor = MISC_DYNAMIC_MINOR;
	cdev->dev.name = "iomux";
	cdev->dev.fops = &rk_iomux_fops;
	cdev->dev.parent = NULL;
	ret = misc_register(&cdev->dev);
	if (ret) {
		pr_err("failed to register iomux device (%d)\n", ret);
		return ret;
	}

	return 0;
}
late_initcall(rk_iomux_device_create);
