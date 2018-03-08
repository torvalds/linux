/*
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

static struct platform_device *omap_drm_device;

static int __init omap_dss_init(void)
{
	int r;

	r = platform_register_drivers(omap_dss_drivers,
				      ARRAY_SIZE(omap_dss_drivers));
	if (r)
		goto err_reg;

	omap_drm_device = platform_device_register_simple("omapdrm", 0, NULL, 0);
	if (IS_ERR(omap_drm_device)) {
		r = PTR_ERR(omap_drm_device);
		goto err_reg;
	}

	return 0;

err_reg:
	platform_unregister_drivers(omap_dss_drivers,
				    ARRAY_SIZE(omap_dss_drivers));

	return r;
}

static void __exit omap_dss_exit(void)
{
	platform_device_unregister(omap_drm_device);

	platform_unregister_drivers(omap_dss_drivers,
				    ARRAY_SIZE(omap_dss_drivers));
}

module_init(omap_dss_init);
module_exit(omap_dss_exit);

MODULE_AUTHOR("Tomi Valkeinen <tomi.valkeinen@nokia.com>");
MODULE_DESCRIPTION("OMAP2/3 Display Subsystem");
MODULE_LICENSE("GPL v2");

