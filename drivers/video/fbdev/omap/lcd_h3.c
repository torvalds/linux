/*
 * LCD panel support for the TI OMAP H3 board
 *
 * Copyright (C) 2004 Nokia Corporation
 * Author: Imre Deak <imre.deak@nokia.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
 */

#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/mfd/tps65010.h>
#include <linux/gpio.h>

#include "omapfb.h"

#define MODULE_NAME	"omapfb-lcd_h3"

static int h3_panel_enable(struct lcd_panel *panel)
{
	int r = 0;

	/* GPIO1 and GPIO2 of TPS65010 send LCD_ENBKL and LCD_ENVDD signals */
	r = tps65010_set_gpio_out_value(GPIO1, HIGH);
	if (!r)
		r = tps65010_set_gpio_out_value(GPIO2, HIGH);
	if (r)
		pr_err(MODULE_NAME ": Unable to turn on LCD panel\n");

	return r;
}

static void h3_panel_disable(struct lcd_panel *panel)
{
	int r = 0;

	/* GPIO1 and GPIO2 of TPS65010 send LCD_ENBKL and LCD_ENVDD signals */
	r = tps65010_set_gpio_out_value(GPIO1, LOW);
	if (!r)
		tps65010_set_gpio_out_value(GPIO2, LOW);
	if (r)
		pr_err(MODULE_NAME ": Unable to turn off LCD panel\n");
}

static struct lcd_panel h3_panel = {
	.name		= "h3",
	.config		= OMAP_LCDC_PANEL_TFT,

	.data_lines	= 16,
	.bpp		= 16,
	.x_res		= 240,
	.y_res		= 320,
	.pixel_clock	= 12000,
	.hsw		= 12,
	.hfp		= 14,
	.hbp		= 72 - 12,
	.vsw		= 1,
	.vfp		= 1,
	.vbp		= 0,
	.pcd		= 0,

	.enable		= h3_panel_enable,
	.disable	= h3_panel_disable,
};

static int h3_panel_probe(struct platform_device *pdev)
{
	omapfb_register_panel(&h3_panel);
	return 0;
}

static struct platform_driver h3_panel_driver = {
	.probe		= h3_panel_probe,
	.driver		= {
		.name	= "lcd_h3",
	},
};

module_platform_driver(h3_panel_driver);
