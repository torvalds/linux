// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * LCD panel support for the TI OMAP OSK board
 *
 * Copyright (C) 2004 Nokia Corporation
 * Author: Imre Deak <imre.deak@nokia.com>
 * Adapted for OSK by <dirk.behme@de.bosch.com>
 */

#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/gpio.h>

#include <mach/hardware.h>
#include <mach/mux.h>

#include "omapfb.h"

static int osk_panel_enable(struct lcd_panel *panel)
{
	/* configure PWL pin */
	omap_cfg_reg(PWL);

	/* Enable PWL unit */
	omap_writeb(0x01, OMAP_PWL_CLK_ENABLE);

	/* Set PWL level */
	omap_writeb(0xFF, OMAP_PWL_ENABLE);

	/* set GPIO2 high (lcd power enabled) */
	gpio_set_value(2, 1);

	return 0;
}

static void osk_panel_disable(struct lcd_panel *panel)
{
	/* Set PWL level to zero */
	omap_writeb(0x00, OMAP_PWL_ENABLE);

	/* Disable PWL unit */
	omap_writeb(0x00, OMAP_PWL_CLK_ENABLE);

	/* set GPIO2 low */
	gpio_set_value(2, 0);
}

static struct lcd_panel osk_panel = {
	.name		= "osk",
	.config		= OMAP_LCDC_PANEL_TFT,

	.bpp		= 16,
	.data_lines	= 16,
	.x_res		= 240,
	.y_res		= 320,
	.pixel_clock	= 12500,
	.hsw		= 40,
	.hfp		= 40,
	.hbp		= 72,
	.vsw		= 1,
	.vfp		= 1,
	.vbp		= 0,
	.pcd		= 12,

	.enable		= osk_panel_enable,
	.disable	= osk_panel_disable,
};

static int osk_panel_probe(struct platform_device *pdev)
{
	omapfb_register_panel(&osk_panel);
	return 0;
}

static struct platform_driver osk_panel_driver = {
	.probe		= osk_panel_probe,
	.driver		= {
		.name	= "lcd_osk",
	},
};

module_platform_driver(osk_panel_driver);

MODULE_AUTHOR("Imre Deak");
MODULE_DESCRIPTION("LCD panel support for the TI OMAP OSK board");
MODULE_LICENSE("GPL");
