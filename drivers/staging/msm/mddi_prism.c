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

static int prism_lcd_on(struct platform_device *pdev);
static int prism_lcd_off(struct platform_device *pdev);

static int prism_lcd_on(struct platform_device *pdev)
{
	/* Set the MDP pixel data attributes for Primary Display */
	mddi_host_write_pix_attr_reg(0x00C3);

	return 0;
}

static int prism_lcd_off(struct platform_device *pdev)
{
	return 0;
}

static int __init prism_probe(struct platform_device *pdev)
{
	msm_fb_add_device(pdev);

	return 0;
}

static struct platform_driver this_driver = {
	.probe  = prism_probe,
	.driver = {
		.name   = "mddi_prism_wvga",
	},
};

static struct msm_fb_panel_data prism_panel_data = {
	.on = prism_lcd_on,
	.off = prism_lcd_off,
};

static struct platform_device this_device = {
	.name   = "mddi_prism_wvga",
	.id	= 0,
	.dev	= {
		.platform_data = &prism_panel_data,
	}
};

static int __init prism_init(void)
{
	int ret;
	struct msm_panel_info *pinfo;

#ifdef CONFIG_FB_MSM_MDDI_AUTO_DETECT
	u32 id;

	ret = msm_fb_detect_client("mddi_prism_wvga");
	if (ret == -ENODEV)
		return 0;

	if (ret) {
		id = mddi_get_client_id();

		if (((id >> 16) != 0x4474) || ((id & 0xffff) == 0x8960))
			return 0;
	}
#endif
	ret = platform_driver_register(&this_driver);
	if (!ret) {
		pinfo = &prism_panel_data.panel_info;
		pinfo->xres = 800;
		pinfo->yres = 480;
		pinfo->type = MDDI_PANEL;
		pinfo->pdest = DISPLAY_1;
		pinfo->mddi.vdopkt = MDDI_DEFAULT_PRIM_PIX_ATTR;
		pinfo->wait_cycle = 0;
		pinfo->bpp = 18;
		pinfo->fb_num = 2;
		pinfo->clk_rate = 153600000;
		pinfo->clk_min = 150000000;
		pinfo->clk_max = 160000000;
		pinfo->lcd.vsync_enable = TRUE;
		pinfo->lcd.refx100 = 6050;
		pinfo->lcd.v_back_porch = 23;
		pinfo->lcd.v_front_porch = 20;
		pinfo->lcd.v_pulse_width = 105;
		pinfo->lcd.hw_vsync_mode = TRUE;
		pinfo->lcd.vsync_notifier_period = 0;

		ret = platform_device_register(&this_device);
		if (ret)
			platform_driver_unregister(&this_driver);
	}

	return ret;
}

module_init(prism_init);
