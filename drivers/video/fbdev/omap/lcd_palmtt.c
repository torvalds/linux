/*
 * LCD panel support for Palm Tungsten|T
 * Current version : Marek Vasut <marek.vasut@gmail.com>
 *
 * Modified from lcd_inn1510.c
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

/*
GPIO11 - backlight
GPIO12 - screen blanking
GPIO13 - screen blanking
*/

#include <linux/platform_device.h>
#include <linux/module.h>
#include <linux/io.h>
#include <linux/gpio.h>

#include "omapfb.h"

static unsigned long palmtt_panel_get_caps(struct lcd_panel *panel)
{
	return OMAPFB_CAPS_SET_BACKLIGHT;
}

static struct lcd_panel palmtt_panel = {
	.name		= "palmtt",
	.config		= OMAP_LCDC_PANEL_TFT | OMAP_LCDC_INV_VSYNC |
			OMAP_LCDC_INV_HSYNC | OMAP_LCDC_HSVS_RISING_EDGE |
			OMAP_LCDC_HSVS_OPPOSITE,
	.bpp		= 16,
	.data_lines	= 16,
	.x_res		= 320,
	.y_res		= 320,
	.pixel_clock	= 10000,
	.hsw		= 4,
	.hfp		= 8,
	.hbp		= 28,
	.vsw		= 1,
	.vfp		= 8,
	.vbp		= 7,
	.pcd		= 0,

	.get_caps	= palmtt_panel_get_caps,
};

static int palmtt_panel_probe(struct platform_device *pdev)
{
	omapfb_register_panel(&palmtt_panel);
	return 0;
}

static struct platform_driver palmtt_panel_driver = {
	.probe		= palmtt_panel_probe,
	.driver		= {
		.name	= "lcd_palmtt",
	},
};

module_platform_driver(palmtt_panel_driver);
