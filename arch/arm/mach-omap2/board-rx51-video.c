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

static struct panel_acx565akm_data lcd_data = {
	.reset_gpio	= RX51_LCD_RESET_GPIO,
};

static struct omap_dss_device rx51_lcd_device = {
	.name			= "lcd",
	.driver_name		= "panel-acx565akm",
	.type			= OMAP_DISPLAY_TYPE_SDI,
	.phy.sdi.datapairs	= 2,
	.data			= &lcd_data,
};

static struct omap_dss_device  rx51_tv_device = {
	.name			= "tv",
	.type			= OMAP_DISPLAY_TYPE_VENC,
	.driver_name		= "venc",
	.phy.venc.type	        = OMAP_DSS_VENC_TYPE_COMPOSITE,
};

static struct omap_dss_device *rx51_dss_devices[] = {
	&rx51_lcd_device,
	&rx51_tv_device,
};

static struct omap_dss_board_info rx51_dss_board_info = {
	.num_devices	= ARRAY_SIZE(rx51_dss_devices),
	.devices	= rx51_dss_devices,
	.default_device	= &rx51_lcd_device,
};

static int __init rx51_video_init(void)
{
	if (!machine_is_nokia_rx51())
		return 0;

	if (omap_mux_init_gpio(RX51_LCD_RESET_GPIO, OMAP_PIN_OUTPUT)) {
		pr_err("%s cannot configure MUX for LCD RESET\n", __func__);
		return 0;
	}

	omap_display_init(&rx51_dss_board_info);

	return 0;
}

omap_subsys_initcall(rx51_video_init);
#endif /* defined(CONFIG_FB_OMAP2) || defined(CONFIG_FB_OMAP2_MODULE) */
