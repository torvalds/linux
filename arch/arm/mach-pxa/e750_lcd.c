/* e750_lcd.c
 *
 * This file contains the definitions for the LCD timings and functions
 * to control the LCD power / frontlighting via the w100fb driver.
 *
 * (c) 2005 Ian Molton <spyro@f2s.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */

#include <linux/module.h>
#include <linux/device.h>
#include <linux/fb.h>
#include <linux/err.h>
#include <linux/platform_device.h>

#include <asm/mach-types.h>

#include <video/w100fb.h>

static struct w100_gen_regs e750_lcd_regs = {
	.lcd_format =            0x00008003,
	.lcdd_cntl1 =            0x00000000,
	.lcdd_cntl2 =            0x0003ffff,
	.genlcd_cntl1 =          0x00fff003,
	.genlcd_cntl2 =          0x003c0f03,
	.genlcd_cntl3 =          0x000143aa,
};

static struct w100_mode e750_lcd_mode = {
	.xres            = 240,
	.yres            = 320,
	.left_margin     = 21,
	.right_margin    = 22,
	.upper_margin    = 5,
	.lower_margin    = 4,
	.crtc_ss         = 0x80150014,
	.crtc_ls         = 0x8014000d,
	.crtc_gs         = 0xc1000005,
	.crtc_vpos_gs    = 0x00020147,
	.crtc_rev        = 0x0040010a,
	.crtc_dclk       = 0xa1700030,
	.crtc_gclk       = 0x80cc0015,
	.crtc_goe        = 0x80cc0015,
	.crtc_ps1_active = 0x61060017,
	.pll_freq        = 57,
	.pixclk_divider         = 4,
	.pixclk_divider_rotated = 4,
	.pixclk_src     = CLK_SRC_XTAL,
	.sysclk_divider  = 1,
	.sysclk_src     = CLK_SRC_PLL,
};


static struct w100_gpio_regs e750_w100_gpio_info = {
	.init_data1 = 0x01192f1b,
	.gpio_dir1  = 0xd5ffdeff,
	.gpio_oe1   = 0x000020bf,
	.init_data2 = 0x010f010f,
	.gpio_dir2  = 0xffffffff,
	.gpio_oe2   = 0x000001cf,
};

static struct w100fb_mach_info e750_fb_info = {
	.modelist   = &e750_lcd_mode,
	.num_modes  = 1,
	.regs       = &e750_lcd_regs,
	.gpio       = &e750_w100_gpio_info,
	.xtal_freq  = 14318000,
	.xtal_dbl   = 1,
};

static struct resource e750_fb_resources[] = {
	[0] = {
		.start          = 0x0c000000,
		.end            = 0x0cffffff,
		.flags          = IORESOURCE_MEM,
	},
};

/* ----------------------- device declarations -------------------------- */


static struct platform_device e750_fb_device = {
	.name           = "w100fb",
	.id             = -1,
	.dev            = {
		.platform_data  = &e750_fb_info,
	},
	.num_resources  = ARRAY_SIZE(e750_fb_resources),
	.resource       = e750_fb_resources,
};

static int e750_lcd_init(void)
{
	if (!machine_is_e750())
		return -ENODEV;

	return platform_device_register(&e750_fb_device);
}

module_init(e750_lcd_init);

MODULE_AUTHOR("Ian Molton <spyro@f2s.com>");
MODULE_DESCRIPTION("e750 lcd driver");
MODULE_LICENSE("GPLv2");
