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
#include <plat/vram.h>
#include <linux/platform_data/spi-omap2-mcspi.h>

#include <mach/board-rx51.h>

#include "mux.h"

#define RX51_LCD_RESET_GPIO	90

#if defined(CONFIG_FB_OMAP2) || defined(CONFIG_FB_OMAP2_MODULE)

static int rx51_lcd_enable(struct omap_dss_device *dssdev)
{
	gpio_set_value(dssdev->reset_gpio, 1);
	return 0;
}

static void rx51_lcd_disable(struct omap_dss_device *dssdev)
{
	gpio_set_value(dssdev->reset_gpio, 0);
}

static struct omap_dss_device rx51_lcd_device = {
	.name			= "lcd",
	.driver_name		= "panel-acx565akm",
	.type			= OMAP_DISPLAY_TYPE_SDI,
	.phy.sdi.datapairs	= 2,
	.reset_gpio		= RX51_LCD_RESET_GPIO,
	.platform_enable	= rx51_lcd_enable,
	.platform_disable	= rx51_lcd_disable,
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

	if (gpio_request_one(RX51_LCD_RESET_GPIO, GPIOF_OUT_INIT_HIGH,
			     "LCD ACX565AKM reset")) {
		pr_err("%s failed to get LCD Reset GPIO\n", __func__);
		return 0;
	}

	omap_display_init(&rx51_dss_board_info);
	return 0;
}

subsys_initcall(rx51_video_init);

void __init rx51_video_mem_init(void)
{
	/*
	 * GFX 864x480x32bpp
	 * VID1/2 1280x720x32bpp double buffered
	 */
	omap_vram_set_sdram_vram(PAGE_ALIGN(864 * 480 * 4) +
			2 * PAGE_ALIGN(1280 * 720 * 4 * 2), 0);
}

#else
void __init rx51_video_mem_init(void) { }
#endif /* defined(CONFIG_FB_OMAP2) || defined(CONFIG_FB_OMAP2_MODULE) */
