// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright 2024 NXP
 */

#include <linux/module.h>
#include <linux/platform_device.h>

#include "dc-drv.h"

static struct platform_driver * const dc_drivers[] = {
	&dc_de_driver,
	&dc_fg_driver,
	&dc_tc_driver,
};

static int __init dc_drm_init(void)
{
	return platform_register_drivers(dc_drivers, ARRAY_SIZE(dc_drivers));
}

static void __exit dc_drm_exit(void)
{
	platform_unregister_drivers(dc_drivers, ARRAY_SIZE(dc_drivers));
}

module_init(dc_drm_init);
module_exit(dc_drm_exit);

MODULE_DESCRIPTION("i.MX8 Display Controller DRM Driver");
MODULE_AUTHOR("Liu Ying <victor.liu@nxp.com>");
MODULE_LICENSE("GPL");
