/* Copyright (c) 2009, Code Aurora Forum. All rights reserved.
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
#include "mddi_toshiba.h"

static int __init mddi_toshiba_wvga_pt_init(void)
{
	int ret;
	struct msm_panel_info pinfo;
#ifdef CONFIG_FB_MSM_MDDI_AUTO_DETECT
	uint id;

	ret = msm_fb_detect_client("mddi_toshiba_wvga_pt");
	if (ret == -ENODEV)
		return 0;

	if (ret) {
		id = mddi_get_client_id();
		if (id != 0xd2638722)
			return 0;
	}
#endif

	pinfo.xres = 480;
	pinfo.yres = 800;
	pinfo.type = MDDI_PANEL;
	pinfo.pdest = DISPLAY_1;
	pinfo.mddi.vdopkt = MDDI_DEFAULT_PRIM_PIX_ATTR;
	pinfo.wait_cycle = 0;
	pinfo.bpp = 18;
	pinfo.lcd.vsync_enable = FALSE;
	pinfo.bl_max = 15;
	pinfo.bl_min = 1;
	pinfo.clk_rate = 192000000;
	pinfo.clk_min =  190000000;
	pinfo.clk_max =  200000000;
	pinfo.fb_num = 2;

	ret = mddi_toshiba_device_register(&pinfo, TOSHIBA_VGA_PRIM,
						LCD_TOSHIBA_2P4_WVGA_PT);
	if (ret)
		printk(KERN_ERR "%s: failed to register device!\n", __func__);

	return ret;
}

module_init(mddi_toshiba_wvga_pt_init);
