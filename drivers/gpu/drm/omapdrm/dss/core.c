// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2009 Nokia Corporation
 * Author: Tomi Valkeinen <tomi.valkeinen@ti.com>
 *
 * Some code and ideas taken from drivers/video/omap/ driver
 * by Imre Deak.
 */

#define DSS_SUBSYS_NAME "CORE"

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/platform_device.h>

#include "omapdss.h"
#include "dss.h"

/* INIT */
static struct platform_driver * const omap_dss_drivers[] = {
	&omap_dsshw_driver,
	&omap_dispchw_driver,
#ifdef CONFIG_OMAP2_DSS_DSI
	&omap_dsihw_driver,
#endif
#ifdef CONFIG_OMAP2_DSS_VENC
	&omap_venchw_driver,
#endif
#ifdef CONFIG_OMAP4_DSS_HDMI
	&omapdss_hdmi4hw_driver,
#endif
#ifdef CONFIG_OMAP5_DSS_HDMI
	&omapdss_hdmi5hw_driver,
#endif
};

static int __init omap_dss_init(void)
{
	return platform_register_drivers(omap_dss_drivers,
					 ARRAY_SIZE(omap_dss_drivers));
}

static void __exit omap_dss_exit(void)
{
	platform_unregister_drivers(omap_dss_drivers,
				    ARRAY_SIZE(omap_dss_drivers));
}

module_init(omap_dss_init);
module_exit(omap_dss_exit);

MODULE_AUTHOR("Tomi Valkeinen <tomi.valkeinen@ti.com>");
MODULE_DESCRIPTION("OMAP2/3 Display Subsystem");
MODULE_LICENSE("GPL v2");

