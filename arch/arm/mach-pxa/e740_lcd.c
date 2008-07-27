/* e740_lcd.c
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

/*
**potential** shutdown routine - to be investigated
devmem2 0x0c010528 w 0xff3fff00
devmem2 0x0c010190 w 0x7FFF8000
devmem2 0x0c0101b0 w 0x00FF0000
devmem2 0x0c01008c w 0x00000000
devmem2 0x0c010080 w 0x000000bf
devmem2 0x0c010098 w 0x00000015
devmem2 0x0c010088 w 0x4b000204
devmem2 0x0c010098 w 0x0000001d
*/

static struct w100_gen_regs e740_lcd_regs = {
	.lcd_format =            0x00008023,
	.lcdd_cntl1 =            0x0f000000,
	.lcdd_cntl2 =            0x0003ffff,
	.genlcd_cntl1 =          0x00ffff03,
	.genlcd_cntl2 =          0x003c0f03,
	.genlcd_cntl3 =          0x000143aa,
};

static struct w100_mode e740_lcd_mode = {
	.xres            = 240,
	.yres            = 320,
	.left_margin     = 20,
	.right_margin    = 28,
	.upper_margin    = 9,
	.lower_margin    = 8,
	.crtc_ss         = 0x80140013,
	.crtc_ls         = 0x81150110,
	.crtc_gs         = 0x80050005,
	.crtc_vpos_gs    = 0x000a0009,
	.crtc_rev        = 0x0040010a,
	.crtc_dclk       = 0xa906000a,
	.crtc_gclk       = 0x80050108,
	.crtc_goe        = 0x80050108,
	.pll_freq        = 57,
	.pixclk_divider         = 4,
	.pixclk_divider_rotated = 4,
	.pixclk_src     = CLK_SRC_XTAL,
	.sysclk_divider  = 1,
	.sysclk_src     = CLK_SRC_PLL,
	.crtc_ps1_active =       0x41060010,
};


static struct w100_gpio_regs e740_w100_gpio_info = {
	.init_data1 = 0x21002103,
	.gpio_dir1  = 0xffffdeff,
	.gpio_oe1   = 0x03c00643,
	.init_data2 = 0x003f003f,
	.gpio_dir2  = 0xffffffff,
	.gpio_oe2   = 0x000000ff,
};

static struct w100fb_mach_info e740_fb_info = {
	.modelist   = &e740_lcd_mode,
	.num_modes  = 1,
	.regs       = &e740_lcd_regs,
	.gpio       = &e740_w100_gpio_info,
	.xtal_freq = 14318000,
	.xtal_dbl   = 1,
};

static struct resource e740_fb_resources[] = {
	[0] = {
		.start          = 0x0c000000,
		.end            = 0x0cffffff,
		.flags          = IORESOURCE_MEM,
	},
};

/* ----------------------- device declarations -------------------------- */


static struct platform_device e740_fb_device = {
	.name           = "w100fb",
	.id             = -1,
	.dev            = {
		.platform_data  = &e740_fb_info,
	},
	.num_resources  = ARRAY_SIZE(e740_fb_resources),
	.resource       = e740_fb_resources,
};

static int e740_lcd_init(void)
{
	int ret;

	if (!machine_is_e740())
		return -ENODEV;

	return platform_device_register(&e740_fb_device);
}

module_init(e740_lcd_init);

MODULE_AUTHOR("Ian Molton <spyro@f2s.com>");
MODULE_DESCRIPTION("e740 lcd driver");
MODULE_LICENSE("GPLv2");
