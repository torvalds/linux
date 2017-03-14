/*
 * LCD panel support for the Palm Tungsten E
 *
 * Original version : Romain Goyet <r.goyet@gmail.com>
 * Current version : Laurent Gonzalez <palmte.linux@free.fr>
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
#include <linux/io.h>

#include "omapfb.h"

static struct lcd_panel palmte_panel = {
	.name		= "palmte",
	.config		= OMAP_LCDC_PANEL_TFT | OMAP_LCDC_INV_VSYNC |
			  OMAP_LCDC_INV_HSYNC | OMAP_LCDC_HSVS_RISING_EDGE |
			  OMAP_LCDC_HSVS_OPPOSITE,

	.data_lines	= 16,
	.bpp		= 8,
	.pixel_clock	= 12000,
	.x_res		= 320,
	.y_res		= 320,
	.hsw		= 4,
	.hfp		= 8,
	.hbp		= 28,
	.vsw		= 1,
	.vfp		= 8,
	.vbp		= 7,
	.pcd		= 0,
};

static int palmte_panel_probe(struct platform_device *pdev)
{
	omapfb_register_panel(&palmte_panel);
	return 0;
}

static struct platform_driver palmte_panel_driver = {
	.probe		= palmte_panel_probe,
	.driver		= {
		.name	= "lcd_palmte",
	},
};

module_platform_driver(palmte_panel_driver);
