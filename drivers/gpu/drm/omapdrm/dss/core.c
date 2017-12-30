/*
 * linux/drivers/video/omap2/dss/core.c
 *
 * Copyright (C) 2009 Nokia Corporation
 * Author: Tomi Valkeinen <tomi.valkeinen@nokia.com>
 *
 * Some code and ideas taken from drivers/video/omap/ driver
 * by Imre Deak.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#define DSS_SUBSYS_NAME "CORE"

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/platform_device.h>

#include "omapdss.h"
#include "dss.h"

/* INIT */
static int (*dss_output_drv_reg_funcs[])(void) __initdata = {
	dss_init_platform_driver,
	dispc_init_platform_driver,
#ifdef CONFIG_OMAP2_DSS_DSI
	dsi_init_platform_driver,
#endif
#ifdef CONFIG_OMAP2_DSS_VENC
	venc_init_platform_driver,
#endif
#ifdef CONFIG_OMAP4_DSS_HDMI
	hdmi4_init_platform_driver,
#endif
#ifdef CONFIG_OMAP5_DSS_HDMI
	hdmi5_init_platform_driver,
#endif
};

static void (*dss_output_drv_unreg_funcs[])(void) = {
#ifdef CONFIG_OMAP5_DSS_HDMI
	hdmi5_uninit_platform_driver,
#endif
#ifdef CONFIG_OMAP4_DSS_HDMI
	hdmi4_uninit_platform_driver,
#endif
#ifdef CONFIG_OMAP2_DSS_VENC
	venc_uninit_platform_driver,
#endif
#ifdef CONFIG_OMAP2_DSS_DSI
	dsi_uninit_platform_driver,
#endif
	dispc_uninit_platform_driver,
	dss_uninit_platform_driver,
};

static struct platform_device *omap_drm_device;

static int __init omap_dss_init(void)
{
	int r;
	int i;

	for (i = 0; i < ARRAY_SIZE(dss_output_drv_reg_funcs); ++i) {
		r = dss_output_drv_reg_funcs[i]();
		if (r)
			goto err_reg;
	}

	omap_drm_device = platform_device_register_simple("omapdrm", 0, NULL, 0);
	if (IS_ERR(omap_drm_device)) {
		r = PTR_ERR(omap_drm_device);
		goto err_reg;
	}

	return 0;

err_reg:
	for (i = ARRAY_SIZE(dss_output_drv_reg_funcs) - i;
			i < ARRAY_SIZE(dss_output_drv_reg_funcs);
			++i)
		dss_output_drv_unreg_funcs[i]();

	return r;
}

static void __exit omap_dss_exit(void)
{
	int i;

	platform_device_unregister(omap_drm_device);

	for (i = 0; i < ARRAY_SIZE(dss_output_drv_unreg_funcs); ++i)
		dss_output_drv_unreg_funcs[i]();
}

module_init(omap_dss_init);
module_exit(omap_dss_exit);

MODULE_AUTHOR("Tomi Valkeinen <tomi.valkeinen@nokia.com>");
MODULE_DESCRIPTION("OMAP2/3 Display Subsystem");
MODULE_LICENSE("GPL v2");

