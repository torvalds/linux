/*
 *  linux/arch/arm/mach-aaec2000/aaed2000.c
 *
 *  Support for the Agilent AAED-2000 Development Platform.
 *
 *  Copyright (c) 2005 Nicolas Bellido Y Ortega
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 2 as
 *  published by the Free Software Foundation.
 *
 */
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/device.h>
#include <linux/major.h>
#include <linux/interrupt.h>

#include <asm/setup.h>
#include <asm/memory.h>
#include <asm/mach-types.h>
#include <asm/hardware.h>
#include <asm/irq.h>

#include <asm/mach/arch.h>
#include <asm/mach/map.h>
#include <asm/mach/irq.h>

#include <asm/arch/aaed2000.h>

#include "core.h"

static void aaed2000_clcd_disable(struct clcd_fb *fb)
{
	AAED_EXT_GPIO &= ~AAED_EGPIO_LCD_PWR_EN;
}

static void aaed2000_clcd_enable(struct clcd_fb *fb)
{
	AAED_EXT_GPIO |= AAED_EGPIO_LCD_PWR_EN;
}

struct aaec2000_clcd_info clcd_info = {
	.enable = aaed2000_clcd_enable,
	.disable = aaed2000_clcd_disable,
	.panel = {
		.mode	= {
			.name		= "Sharp",
			.refresh	= 60,
			.xres		= 640,
			.yres		= 480,
			.pixclock	= 39721,
			.left_margin	= 20,
			.right_margin	= 44,
			.upper_margin	= 21,
			.lower_margin	= 34,
			.hsync_len	= 96,
			.vsync_len	= 2,
			.sync		= 0,
			.vmode	= FB_VMODE_NONINTERLACED,
		},
		.width	= -1,
		.height	= -1,
		.tim2	= TIM2_IVS | TIM2_IHS,
		.cntl	= CNTL_LCDTFT,
		.bpp	= 16,
	},
};

static void __init aaed2000_init_irq(void)
{
	aaec2000_init_irq();
}

static void __init aaed2000_init(void)
{
	aaec2000_set_clcd_plat_data(&clcd_info);
}

static struct map_desc aaed2000_io_desc[] __initdata = {
  { EXT_GPIO_VBASE, EXT_GPIO_PBASE, EXT_GPIO_LENGTH, MT_DEVICE }, /* Ext GPIO */
};

static void __init aaed2000_map_io(void)
{
	aaec2000_map_io();
	iotable_init(aaed2000_io_desc, ARRAY_SIZE(aaed2000_io_desc));
}

MACHINE_START(AAED2000, "Agilent AAED-2000 Development Platform")
	/* Maintainer: Nicolas Bellido Y Ortega */
	.phys_ram	= 0xf0000000,
	.phys_io	= PIO_BASE,
	.io_pg_offst	= ((VIO_BASE) >> 18) & 0xfffc,
	.map_io		= aaed2000_map_io,
	.init_irq	= aaed2000_init_irq,
	.timer		= &aaec2000_timer,
	.init_machine	= aaed2000_init,
MACHINE_END
