/*
 * linux/arch/arm/mach-w90x900/mach-nuc950evb.c
 *
 * Based on mach-s3c2410/mach-smdk2410.c by Jonas Dietsche
 *
 * Copyright (C) 2008 Nuvoton technology corporation.
 *
 * Wan ZongShun <mcuos.com@gmail.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation;version 2 of the License.
 *   history:
 *     Wang Qiang (rurality.linux@gmail.com) add LCD support
 *
 */

#include <linux/platform_device.h>
#include <asm/mach/arch.h>
#include <asm/mach/map.h>
#include <asm/mach-types.h>
#include <mach/map.h>
#include <mach/regs-ldm.h>
#include <mach/fb.h>

#include "nuc950.h"

#ifdef CONFIG_FB_NUC900
/* LCD Controller */
static struct nuc900fb_display __initdata nuc950_lcd_info[] = {
	/* Giantplus Technology GPM1040A0 320x240 Color TFT LCD */
	[0] = {
		.type		= LCM_DCCS_VA_SRC_RGB565,
		.width 		= 320,
		.height		= 240,
		.xres		= 320,
		.yres		= 240,
		.bpp		= 16,
		.pixclock	= 200000,
		.left_margin	= 34,
		.right_margin   = 54,
		.hsync_len	= 10,
		.upper_margin	= 18,
		.lower_margin	= 4,
		.vsync_len	= 1,
		.dccs		= 0x8e00041a,
		.devctl		= 0x060800c0,
		.fbctrl		= 0x00a000a0,
		.scale		= 0x04000400,
	},
};

static struct nuc900fb_mach_info nuc950_fb_info __initdata = {
#if defined(CONFIG_GPM1040A0_320X240)
	.displays		= &nuc950_lcd_info[0],
#else
	.displays		= nuc950_lcd_info,
#endif
	.num_displays 		= ARRAY_SIZE(nuc950_lcd_info),
	.default_display	= 0,
	.gpio_dir		= 0x00000004,
	.gpio_dir_mask		= 0xFFFFFFFD,
	.gpio_data		= 0x00000004,
	.gpio_data_mask		= 0xFFFFFFFD,
};
#endif

static void __init nuc950evb_map_io(void)
{
	nuc950_map_io();
	nuc950_init_clocks();
}

static void __init nuc950evb_init(void)
{
	nuc950_board_init();
#ifdef CONFIG_FB_NUC900
	nuc900_fb_set_platdata(&nuc950_fb_info);
#endif
}

MACHINE_START(W90P950EVB, "W90P950EVB")
	/* Maintainer: Wan ZongShun */
	.phys_io	= W90X900_PA_UART,
	.io_pg_offst	= (((u32)W90X900_VA_UART) >> 18) & 0xfffc,
	.boot_params	= 0,
	.map_io		= nuc950evb_map_io,
	.init_irq	= nuc900_init_irq,
	.init_machine	= nuc950evb_init,
	.timer		= &nuc900_timer,
MACHINE_END
