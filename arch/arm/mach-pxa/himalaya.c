/*
 * linux/arch/arm/mach-pxa/himalaya.c
 *
 * Hardware definitions for the HTC Himalaya
 *
 * Based on 2.6.21-hh20's himalaya.c and himalaya_lcd.c
 *
 * Copyright (c) 2008 Zbynek Michl <Zbynek.Michl@seznam.cz>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/device.h>
#include <linux/fb.h>
#include <linux/platform_device.h>

#include <video/w100fb.h>

#include <asm/setup.h>
#include <asm/mach-types.h>
#include <asm/mach/arch.h>

#include <mach/pxa25x.h>

#include "generic.h"

/* ---------------------- Himalaya LCD definitions -------------------- */

static struct w100_gen_regs himalaya_lcd_regs = {
	.lcd_format =        0x00000003,
	.lcdd_cntl1 =        0x00000000,
	.lcdd_cntl2 =        0x0003ffff,
	.genlcd_cntl1 =      0x00fff003,
	.genlcd_cntl2 =      0x00000003,
	.genlcd_cntl3 =      0x000102aa,
};

static struct w100_mode himalaya4_lcd_mode = {
	.xres 		= 240,
	.yres 		= 320,
	.left_margin 	= 0,
	.right_margin 	= 31,
	.upper_margin 	= 15,
	.lower_margin 	= 0,
	.crtc_ss	= 0x80150014,
	.crtc_ls	= 0xa0fb00f7,
	.crtc_gs	= 0xc0080007,
	.crtc_vpos_gs	= 0x00080007,
	.crtc_rev	= 0x0000000a,
	.crtc_dclk	= 0x81700030,
	.crtc_gclk	= 0x8015010f,
	.crtc_goe	= 0x00000000,
	.pll_freq 	= 80,
	.pixclk_divider = 15,
	.pixclk_divider_rotated = 15,
	.pixclk_src     = CLK_SRC_PLL,
	.sysclk_divider = 0,
	.sysclk_src     = CLK_SRC_PLL,
};

static struct w100_mode himalaya6_lcd_mode = {
	.xres 		= 240,
	.yres 		= 320,
	.left_margin 	= 9,
	.right_margin 	= 8,
	.upper_margin 	= 5,
	.lower_margin 	= 4,
	.crtc_ss	= 0x80150014,
	.crtc_ls	= 0xa0fb00f7,
	.crtc_gs	= 0xc0080007,
	.crtc_vpos_gs	= 0x00080007,
	.crtc_rev	= 0x0000000a,
	.crtc_dclk	= 0xa1700030,
	.crtc_gclk	= 0x8015010f,
	.crtc_goe	= 0x00000000,
	.pll_freq 	= 95,
	.pixclk_divider = 0xb,
	.pixclk_divider_rotated = 4,
	.pixclk_src     = CLK_SRC_PLL,
	.sysclk_divider = 1,
	.sysclk_src     = CLK_SRC_PLL,
};

static struct w100_gpio_regs himalaya_w100_gpio_info = {
	.init_data1 = 0xffff0000,	/* GPIO_DATA  */
	.gpio_dir1  = 0x00000000,	/* GPIO_CNTL1 */
	.gpio_oe1   = 0x003c0000,	/* GPIO_CNTL2 */
	.init_data2 = 0x00000000,	/* GPIO_DATA2 */
	.gpio_dir2  = 0x00000000,	/* GPIO_CNTL3 */
	.gpio_oe2   = 0x00000000,	/* GPIO_CNTL4 */
};

static struct w100fb_mach_info himalaya_fb_info = {
	.num_modes  = 1,
	.regs       = &himalaya_lcd_regs,
	.gpio       = &himalaya_w100_gpio_info,
	.xtal_freq = 16000000,
};

static struct resource himalaya_fb_resources[] = {
	[0] = {
		.start	= 0x08000000,
		.end	= 0x08ffffff,
		.flags	= IORESOURCE_MEM,
	},
};

static struct platform_device himalaya_fb_device = {
	.name           = "w100fb",
	.id             = -1,
	.dev            = {
		.platform_data  = &himalaya_fb_info,
	},
	.num_resources  = ARRAY_SIZE(himalaya_fb_resources),
	.resource       = himalaya_fb_resources,
};

/* ----------------------------------------------------------------------- */

static struct platform_device *devices[] __initdata = {
	&himalaya_fb_device,
};

static void __init himalaya_lcd_init(void)
{
	int himalaya_boardid;

	himalaya_boardid = 0x4; /* hardcoded (detection needs ASIC3 functions) */
	printk(KERN_INFO "himalaya LCD Driver init. boardid=%d\n",
		himalaya_boardid);

	switch (himalaya_boardid) {
	case 0x4:
		himalaya_fb_info.modelist = &himalaya4_lcd_mode;
	break;
	case 0x6:
		himalaya_fb_info.modelist = &himalaya6_lcd_mode;
	break;
	default:
		printk(KERN_INFO "himalaya lcd_init: unknown boardid=%d. Using 0x4\n",
			himalaya_boardid);
		himalaya_fb_info.modelist = &himalaya4_lcd_mode;
	}
}

static void __init himalaya_init(void)
{
	pxa_set_ffuart_info(NULL);
	pxa_set_btuart_info(NULL);
	pxa_set_stuart_info(NULL);
	himalaya_lcd_init();
	platform_add_devices(devices, ARRAY_SIZE(devices));
}


MACHINE_START(HIMALAYA, "HTC Himalaya")
	.atag_offset = 0x100,
	.map_io = pxa25x_map_io,
	.init_irq = pxa25x_init_irq,
	.handle_irq = pxa25x_handle_irq,
	.init_machine = himalaya_init,
	.timer = &pxa_timer,
MACHINE_END
