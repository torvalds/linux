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

#include <linux/string.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/io.h>
#include <linux/clk.h>
#include <linux/err.h>

#include <video/omapdss.h>
#include <plat/omap_hwmod.h>
#include <plat/omap_device.h>
#include <plat/omap-pm.h>

#include "control.h"

static struct platform_device omap_display_device = {
	.name          = "omapdss",
	.id            = -1,
	.dev            = {
		.platform_data = NULL,
	},
};

struct omap_dss_hwmod_data {
	const char *oh_name;
	const char *dev_name;
	const int id;
};

static const struct omap_dss_hwmod_data omap2_dss_hwmod_data[] __initdata = {
	{ "dss_core", "omapdss_dss", -1 },
	{ "dss_dispc", "omapdss_dispc", -1 },
	{ "dss_rfbi", "omapdss_rfbi", -1 },
	{ "dss_venc", "omapdss_venc", -1 },
};

static const struct omap_dss_hwmod_data omap3_dss_hwmod_data[] __initdata = {
	{ "dss_core", "omapdss_dss", -1 },
	{ "dss_dispc", "omapdss_dispc", -1 },
	{ "dss_rfbi", "omapdss_rfbi", -1 },
	{ "dss_venc", "omapdss_venc", -1 },
	{ "dss_dsi1", "omapdss_dsi", 0 },
};

static const struct omap_dss_hwmod_data omap4_dss_hwmod_data[] __initdata = {
	{ "dss_core", "omapdss_dss", -1 },
	{ "dss_dispc", "omapdss_dispc", -1 },
	{ "dss_rfbi", "omapdss_rfbi", -1 },
	{ "dss_venc", "omapdss_venc", -1 },
	{ "dss_dsi1", "omapdss_dsi", 0 },
	{ "dss_dsi2", "omapdss_dsi", 1 },
	{ "dss_hdmi", "omapdss_hdmi", -1 },
};

static int omap4_dsi_mux_pads(int dsi_id, unsigned lanes)
{
	u32 enable_mask, enable_shift;
	u32 pipd_mask, pipd_shift;
	u32 reg;

	if (dsi_id == 0) {
		enable_mask = OMAP4_DSI1_LANEENABLE_MASK;
		enable_shift = OMAP4_DSI1_LANEENABLE_SHIFT;
		pipd_mask = OMAP4_DSI1_PIPD_MASK;
		pipd_shift = OMAP4_DSI1_PIPD_SHIFT;
	} else if (dsi_id == 1) {
		enable_mask = OMAP4_DSI2_LANEENABLE_MASK;
		enable_shift = OMAP4_DSI2_LANEENABLE_SHIFT;
		pipd_mask = OMAP4_DSI2_PIPD_MASK;
		pipd_shift = OMAP4_DSI2_PIPD_SHIFT;
	} else {
		return -ENODEV;
	}

	reg = omap4_ctrl_pad_readl(OMAP4_CTRL_MODULE_PAD_CORE_CONTROL_DSIPHY);

	reg &= ~enable_mask;
	reg &= ~pipd_mask;

	reg |= (lanes << enable_shift) & enable_mask;
	reg |= (lanes << pipd_shift) & pipd_mask;

	omap4_ctrl_pad_writel(reg, OMAP4_CTRL_MODULE_PAD_CORE_CONTROL_DSIPHY);

	return 0;
}

static int omap_dsi_enable_pads(int dsi_id, unsigned lane_mask)
{
	if (cpu_is_omap44xx())
		return omap4_dsi_mux_pads(dsi_id, lane_mask);

	return 0;
}

static void omap_dsi_disable_pads(int dsi_id, unsigned lane_mask)
{
	if (cpu_is_omap44xx())
		omap4_dsi_mux_pads(dsi_id, 0);
}

int __init omap_display_init(struct omap_dss_board_info *board_data)
{
	int r = 0;
	struct omap_hwmod *oh;
	struct platform_device *pdev;
	int i, oh_count;
	struct omap_display_platform_data pdata;
	const struct omap_dss_hwmod_data *curr_dss_hwmod;

	memset(&pdata, 0, sizeof(pdata));

	if (cpu_is_omap24xx()) {
		curr_dss_hwmod = omap2_dss_hwmod_data;
		oh_count = ARRAY_SIZE(omap2_dss_hwmod_data);
	} else if (cpu_is_omap34xx()) {
		curr_dss_hwmod = omap3_dss_hwmod_data;
		oh_count = ARRAY_SIZE(omap3_dss_hwmod_data);
	} else {
		curr_dss_hwmod = omap4_dss_hwmod_data;
		oh_count = ARRAY_SIZE(omap4_dss_hwmod_data);
	}

	if (board_data->dsi_enable_pads == NULL)
		board_data->dsi_enable_pads = omap_dsi_enable_pads;
	if (board_data->dsi_disable_pads == NULL)
		board_data->dsi_disable_pads = omap_dsi_disable_pads;

	pdata.board_data = board_data;
	pdata.board_data->get_context_loss_count =
		omap_pm_get_dev_context_loss_count;

	for (i = 0; i < oh_count; i++) {
		oh = omap_hwmod_lookup(curr_dss_hwmod[i].oh_name);
		if (!oh) {
			pr_err("Could not look up %s\n",
				curr_dss_hwmod[i].oh_name);
			return -ENODEV;
		}

		pdev = omap_device_build(curr_dss_hwmod[i].dev_name,
				curr_dss_hwmod[i].id, oh, &pdata,
				sizeof(struct omap_display_platform_data),
				NULL, 0, 0);

		if (WARN((IS_ERR(pdev)), "Could not build omap_device for %s\n",
				curr_dss_hwmod[i].oh_name))
			return -ENODEV;
	}
	omap_display_device.dev.platform_data = board_data;

	r = platform_device_register(&omap_display_device);
	if (r < 0)
		printk(KERN_ERR "Unable to register OMAP-Display device\n");

	return r;
}
