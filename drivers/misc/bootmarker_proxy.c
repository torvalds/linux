// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2024, Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include <linux/module.h>
#include <linux/printk.h>
#include <linux/device.h>
#include <linux/platform_device.h>
#include <linux/bootmarker_kernel.h>
#include <linux/of_platform.h>
#include <linux/mod_devicetable.h>

static struct bootmarker_drv_ops bootmarker_fun_ops = {0};

int provide_bootmarker_kernel_fun_ops(const struct bootmarker_drv_ops *ops)
{
	if (!ops) {
		pr_err("ops is NULL\n");
		return -EINVAL;
	}
	bootmarker_fun_ops = *ops;
	pr_debug("Boot Marker proxy Ready to be served\n");
	return 0;
}
EXPORT_SYMBOL_GPL(provide_bootmarker_kernel_fun_ops);


int bootmarker_place_marker(const char *name)
{
	int32_t ret = -EPERM;

	if (bootmarker_fun_ops.bootmarker_place_marker) {
		ret = bootmarker_fun_ops.bootmarker_place_marker(name);
		if (ret != 0)
			pr_err("%s: command failed = %d\n", __func__, ret);
	} else {
		pr_err_ratelimited("bootmarker driver is not up yet\n");
		ret = -EAGAIN;
	}
	return ret;
}
EXPORT_SYMBOL_GPL(bootmarker_place_marker);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Boot Marker proxy driver");
