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

static uint32 read_client_reg(uint32 addr)
{
	uint32 val;
	mddi_queue_register_read(addr, &val, TRUE, 0);
	return val;
}

static uint32 toshiba_lcd_gpio_read(void)
{
	uint32 val;

	write_client_reg(GPIODIR, 0x0000000C, TRUE);
	write_client_reg(GPIOSEL, 0x00000000, TRUE);
	write_client_reg(GPIOSEL, 0x00000000, TRUE);
	write_client_reg(GPIOPC, 0x03CF00C0, TRUE);
	val = read_client_reg(GPIODATA) & 0x2C0;

	return val;
}

static u32 mddi_toshiba_panel_detect(void)
{
	mddi_host_type host_idx = MDDI_HOST_PRIM;
	uint32 lcd_gpio;
	u32 mddi_toshiba_lcd = LCD_TOSHIBA_2P4_VGA;

	/* Toshiba display requires larger drive_lo value */
	mddi_host_reg_out(DRIVE_LO, 0x0050);

	lcd_gpio = toshiba_lcd_gpio_read();
	switch (lcd_gpio) {
	case 0x0080:
		mddi_toshiba_lcd = LCD_SHARP_2P4_VGA;
		break;

	case 0x00C0:
	default:
		mddi_toshiba_lcd = LCD_TOSHIBA_2P4_VGA;
		break;
	}

	return mddi_toshiba_lcd;
}

static int __init mddi_toshiba_vga_init(void)
{
	int ret;
	struct msm_panel_info pinfo;
	u32 panel;

#ifdef CONFIG_FB_MSM_MDDI_AUTO_DETECT
	u32 id;

	ret = msm_fb_detect_client("mddi_toshiba_vga");
	if (ret == -ENODEV)
		return 0;

	if (ret) {
		id = mddi_get_client_id();
		if ((id >> 16) != 0xD263)
			return 0;
	}
#endif

	panel = mddi_toshiba_panel_detect();

	pinfo.xres = 480;
	pinfo.yres = 640;
	pinfo.type = MDDI_PANEL;
	pinfo.pdest = DISPLAY_1;
	pinfo.mddi.vdopkt = MDDI_DEFAULT_PRIM_PIX_ATTR;
	pinfo.wait_cycle = 0;
	pinfo.bpp = 18;
	pinfo.lcd.vsync_enable = TRUE;
	pinfo.lcd.refx100 = 6118;
	pinfo.lcd.v_back_porch = 6;
	pinfo.lcd.v_front_porch = 0;
	pinfo.lcd.v_pulse_width = 0;
	pinfo.lcd.hw_vsync_mode = FALSE;
	pinfo.lcd.vsync_notifier_period = (1 * HZ);
	pinfo.bl_max = 99;
	pinfo.bl_min = 1;
	pinfo.clk_rate = 122880000;
	pinfo.clk_min =  120000000;
	pinfo.clk_max =  200000000;
	pinfo.fb_num = 2;

	ret = mddi_toshiba_device_register(&pinfo, TOSHIBA_VGA_PRIM, panel);
	if (ret) {
		printk(KERN_ERR "%s: failed to register device!\n", __func__);
		return ret;
	}

	pinfo.xres = 176;
	pinfo.yres = 220;
	pinfo.type = MDDI_PANEL;
	pinfo.pdest = DISPLAY_2;
	pinfo.mddi.vdopkt = 0x400;
	pinfo.wait_cycle = 0;
	pinfo.bpp = 18;
	pinfo.clk_rate = 122880000;
	pinfo.clk_min =  120000000;
	pinfo.clk_max =  200000000;
	pinfo.fb_num = 2;

	ret = mddi_toshiba_device_register(&pinfo, TOSHIBA_VGA_SECD, panel);
	if (ret)
		printk(KERN_WARNING
			"%s: failed to register device!\n", __func__);

	return ret;
}

module_init(mddi_toshiba_vga_init);
