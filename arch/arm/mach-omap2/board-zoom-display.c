/*
 * Copyright (C) 2010 Texas Instruments Inc.
 *
 * Modified from mach-omap2/board-zoom-peripherals.c
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
#include <linux/platform_data/spi-omap2-mcspi.h>
#include <video/omapdss.h>
#include <video/omap-panel-data.h>

#include "board-zoom.h"
#include "soc.h"
#include "common.h"

#define LCD_PANEL_RESET_GPIO_PROD	96
#define LCD_PANEL_RESET_GPIO_PILOT	55
#define LCD_PANEL_QVGA_GPIO		56

static struct panel_nec_nl8048hl11_platform_data zoom_lcd_pdata = {
	.name                   = "lcd",
	.source                 = "dpi.0",

	.data_lines		= 24,

	.res_gpio		= -1,	/* filled in code */
	.qvga_gpio		= LCD_PANEL_QVGA_GPIO,
};

static struct omap_dss_board_info zoom_dss_data = {
	.default_display_name = "lcd",
};

static void __init zoom_lcd_panel_init(void)
{
	zoom_lcd_pdata.res_gpio = (omap_rev() > OMAP3430_REV_ES3_0) ?
			LCD_PANEL_RESET_GPIO_PROD :
			LCD_PANEL_RESET_GPIO_PILOT;
}

static struct omap2_mcspi_device_config dss_lcd_mcspi_config = {
	.turbo_mode		= 1,
};

static struct spi_board_info nec_8048_spi_board_info[] __initdata = {
	[0] = {
		.modalias		= "panel-nec-nl8048hl11",
		.bus_num		= 1,
		.chip_select		= 2,
		.max_speed_hz		= 375000,
		.controller_data	= &dss_lcd_mcspi_config,
		.platform_data		= &zoom_lcd_pdata,
	},
};

void __init zoom_display_init(void)
{
	omap_display_init(&zoom_dss_data);
	zoom_lcd_panel_init();
	spi_register_board_info(nec_8048_spi_board_info,
				ARRAY_SIZE(nec_8048_spi_board_info));
}

