/*
 * linux/arch/arm/mach-omap2/board-rx51-video.c
 *
 * Copyright (C) 2010 Nokia
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/gpio.h>
#include <linux/spi/spi.h>
#include <linux/mm.h>
#include <asm/mach-types.h>
#include <video/omapdss.h>
#include <video/omap-panel-data.h>

#include <linux/platform_data/spi-omap2-mcspi.h>

#include "soc.h"
#include "board-rx51.h"

#include "mux.h"

#define RX51_LCD_RESET_GPIO	90

#if defined(CONFIG_FB_OMAP2) || defined(CONFIG_FB_OMAP2_MODULE)

static struct connector_atv_platform_data rx51_tv_pdata = {
	.name = "tv",
	.source = "venc.0",
	.connector_type = OMAP_DSS_VENC_TYPE_COMPOSITE,
	.invert_polarity = false,
};

static struct platform_device rx51_tv_connector_device = {
	.name                   = "connector-analog-tv",
	.id                     = 0,
	.dev.platform_data      = &rx51_tv_pdata,
};

static struct omap_dss_board_info rx51_dss_board_info = {
	.default_display_name = "lcd",
};

static int __init rx51_video_init(void)
{
	if (!machine_is_nokia_rx51() && !of_machine_is_compatible("nokia,omap3-n900"))
		return 0;

	if (omap_mux_init_gpio(RX51_LCD_RESET_GPIO, OMAP_PIN_OUTPUT)) {
		pr_err("%s cannot configure MUX for LCD RESET\n", __func__);
		return 0;
	}

	omap_display_init(&rx51_dss_board_info);

	platform_device_register(&rx51_tv_connector_device);

	return 0;
}

omap_subsys_initcall(rx51_video_init);
#endif /* defined(CONFIG_FB_OMAP2) || defined(CONFIG_FB_OMAP2_MODULE) */
