/*
 * OMAP2plus display device setup / initialization.
 *
 * Copyright (C) 2010 Texas Instruments Incorporated - http://www.ti.com/
 *	Senthilvadivu Guruswamy
 *	Sumit Semwal
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed "as is" WITHOUT ANY WARRANTY of any
 * kind, whether express or implied; without even the implied warranty
 * of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/io.h>
#include <linux/clk.h>
#include <linux/err.h>

#include <plat/display.h>
#include <plat/omap_hwmod.h>
#include <plat/omap_device.h>

static struct platform_device omap_display_device = {
	.name          = "omapdss",
	.id            = -1,
	.dev            = {
		.platform_data = NULL,
	},
};

static struct omap_device_pm_latency omap_dss_latency[] = {
	[0] = {
		.deactivate_func        = omap_device_idle_hwmods,
		.activate_func          = omap_device_enable_hwmods,
		.flags			= OMAP_DEVICE_LATENCY_AUTO_ADJUST,
	},
};

int __init omap_display_init(struct omap_dss_board_info *board_data)
{
	int r = 0;
	struct omap_hwmod *oh;
	struct omap_device *od;
	int i;
	struct omap_display_platform_data pdata;

	/*
	 * omap: valid DSS hwmod names
	 * omap2,3: dss_core, dss_dispc, dss_rfbi, dss_venc
	 * omap3: dss_dsi1
	 */
	char *oh_name[] = { "dss_core", "dss_dispc", "dss_rfbi", "dss_venc",
		"dss_dsi1" };
	char *dev_name[] = { "omapdss_dss", "omapdss_dispc", "omapdss_rfbi",
		"omapdss_venc", "omapdss_dsi1" };
	int oh_count;

	memset(&pdata, 0, sizeof(pdata));

	if (cpu_is_omap24xx())
		oh_count = ARRAY_SIZE(oh_name) - 1;
		/* last hwmod dev in oh_name is not available for omap2 */
	else
		oh_count = ARRAY_SIZE(oh_name);

	pdata.board_data = board_data;
	pdata.board_data->get_last_off_on_transaction_id = NULL;

	for (i = 0; i < oh_count; i++) {
		oh = omap_hwmod_lookup(oh_name[i]);
		if (!oh) {
			pr_err("Could not look up %s\n", oh_name[i]);
			return -ENODEV;
		}
		od = omap_device_build(dev_name[i], -1, oh, &pdata,
				sizeof(struct omap_display_platform_data),
				omap_dss_latency,
				ARRAY_SIZE(omap_dss_latency), 0);

		if (WARN((IS_ERR(od)), "Could not build omap_device for %s\n",
				oh_name[i]))
			return -ENODEV;
	}
	omap_display_device.dev.platform_data = board_data;

	r = platform_device_register(&omap_display_device);
	if (r < 0)
		printk(KERN_ERR "Unable to register OMAP-Display device\n");

	return r;
}
