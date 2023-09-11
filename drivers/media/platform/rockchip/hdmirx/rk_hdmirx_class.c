// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2023 Rockchip Electronics Co. Ltd.
 *
 * Author: Shunhua Lan <lsh@rock-chips.com>
 */
#include <linux/fs.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/rk_hdmirx_class.h>

static struct class *hdmirx_class;

struct class *rk_hdmirx_class(void)
{
	return hdmirx_class;
}
EXPORT_SYMBOL(rk_hdmirx_class);

static int __init rk_hdmirx_class_init(void)
{
	hdmirx_class = class_create(THIS_MODULE, "hdmirx");
	if (IS_ERR(hdmirx_class))
		return PTR_ERR(hdmirx_class);
	return 0;
}
subsys_initcall(rk_hdmirx_class_init)

static void __exit rk_hdmirx_class_exit(void)
{
	class_destroy(hdmirx_class);
}
module_exit(rk_hdmirx_class_exit);

MODULE_DESCRIPTION("Rockchip HDMI Receiver Class Driver");
MODULE_LICENSE("GPL");
