/* Copyright (c) 2008-2009, Code Aurora Forum. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301, USA.
 */

#include "msm_fb.h"
#include "mddihost.h"
#include "mddihosti.h"

static int mddi_ext_lcd_on(struct platform_device *pdev);
static int mddi_ext_lcd_off(struct platform_device *pdev);

static int mddi_ext_lcd_on(struct platform_device *pdev)
{
	return 0;
}

static int mddi_ext_lcd_off(struct platform_device *pdev)
{
	return 0;
}

static int __init mddi_ext_lcd_probe(struct platform_device *pdev)
{
	msm_fb_add_device(pdev);

	return 0;
}

static struct platform_driver this_driver = {
	.probe  = mddi_ext_lcd_probe,
	.driver = {
		.name   = "extmddi_svga",
	},
};

static struct msm_fb_panel_data mddi_ext_lcd_panel_data = {
	.panel_info.xres = 800,
	.panel_info.yres = 600,
	.panel_info.type = EXT_MDDI_PANEL,
	.panel_info.pdest = DISPLAY_1,
	.panel_info.wait_cycle = 0,
	.panel_info.bpp = 18,
	.panel_info.fb_num = 2,
	.panel_info.clk_rate = 122880000,
	.panel_info.clk_min  = 120000000,
	.panel_info.clk_max  = 125000000,
	.on = mddi_ext_lcd_on,
	.off = mddi_ext_lcd_off,
};

static struct platform_device this_device = {
	.name   = "extmddi_svga",
	.id	= 0,
	.dev	= {
		.platform_data = &mddi_ext_lcd_panel_data,
	}
};

static int __init mddi_ext_lcd_init(void)
{
	int ret;
	struct msm_panel_info *pinfo;

	ret = platform_driver_register(&this_driver);
	if (!ret) {
		pinfo = &mddi_ext_lcd_panel_data.panel_info;
		pinfo->lcd.vsync_enable = FALSE;
		pinfo->mddi.vdopkt = MDDI_DEFAULT_PRIM_PIX_ATTR;

		ret = platform_device_register(&this_device);
		if (ret)
			platform_driver_unregister(&this_driver);
	}

	return ret;
}

module_init(mddi_ext_lcd_init);
