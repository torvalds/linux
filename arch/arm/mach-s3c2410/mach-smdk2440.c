/* linux/arch/arm/mach-s3c2410/mach-smdk2440.c
 *
 * Copyright (c) 2004,2005 Simtec Electronics
 *	Ben Dooks <ben@simtec.co.uk>
 *
 * http://www.fluff.org/ben/smdk2440/
 *
 * Thanks to Dimity Andric and TomTom for the loan of an SMDK2440.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * Modifications:
 *	01-Nov-2004 BJD   Initial version
 *	12-Nov-2004 BJD   Updated for release
 *	04-Jan-2005 BJD   Fixes for pre-release
 *	22-Feb-2005 BJD   Updated for 2.6.11-rc5 relesa
 *	10-Mar-2005 LCVR  Replaced S3C2410_VA by S3C24XX_VA
 *	14-Mar-2005 BJD	  void __iomem fixes
 *	20-Sep-2005 BJD   Added static to non-exported items
 *	26-Oct-2005 BJD   Added framebuffer data
*/

#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/interrupt.h>
#include <linux/list.h>
#include <linux/timer.h>
#include <linux/init.h>
#include <linux/platform_device.h>

#include <asm/mach/arch.h>
#include <asm/mach/map.h>
#include <asm/mach/irq.h>

#include <asm/hardware.h>
#include <asm/hardware/iomd.h>
#include <asm/io.h>
#include <asm/irq.h>
#include <asm/mach-types.h>

//#include <asm/debug-ll.h>
#include <asm/arch/regs-serial.h>
#include <asm/arch/regs-gpio.h>
#include <asm/arch/regs-lcd.h>

#include <asm/arch/idle.h>
#include <asm/arch/fb.h>

#include "s3c2410.h"
#include "s3c2440.h"
#include "clock.h"
#include "devs.h"
#include "cpu.h"
#include "pm.h"

static struct map_desc smdk2440_iodesc[] __initdata = {
	/* ISA IO Space map (memory space selected by A24) */

	{ (u32)S3C24XX_VA_ISA_WORD, S3C2410_CS2, SZ_16M, MT_DEVICE },
	{ (u32)S3C24XX_VA_ISA_BYTE, S3C2410_CS2, SZ_16M, MT_DEVICE },
};

#define UCON S3C2410_UCON_DEFAULT | S3C2410_UCON_UCLK
#define ULCON S3C2410_LCON_CS8 | S3C2410_LCON_PNONE | S3C2410_LCON_STOPB
#define UFCON S3C2410_UFCON_RXTRIG8 | S3C2410_UFCON_FIFOMODE

static struct s3c2410_uartcfg smdk2440_uartcfgs[] = {
	[0] = {
		.hwport	     = 0,
		.flags	     = 0,
		.ucon	     = 0x3c5,
		.ulcon	     = 0x03,
		.ufcon	     = 0x51,
	},
	[1] = {
		.hwport	     = 1,
		.flags	     = 0,
		.ucon	     = 0x3c5,
		.ulcon	     = 0x03,
		.ufcon	     = 0x51,
	},
	/* IR port */
	[2] = {
		.hwport	     = 2,
		.flags	     = 0,
		.ucon	     = 0x3c5,
		.ulcon	     = 0x43,
		.ufcon	     = 0x51,
	}
};

/* LCD driver info */

static struct s3c2410fb_mach_info smdk2440_lcd_cfg __initdata = {
	.regs	= {

		.lcdcon1	= S3C2410_LCDCON1_TFT16BPP |
				  S3C2410_LCDCON1_TFT |
				  S3C2410_LCDCON1_CLKVAL(0x04),

		.lcdcon2	= S3C2410_LCDCON2_VBPD(7) |
				  S3C2410_LCDCON2_LINEVAL(319) |
				  S3C2410_LCDCON2_VFPD(6) |
				  S3C2410_LCDCON2_VSPW(3),

		.lcdcon3	= S3C2410_LCDCON3_HBPD(19) |
				  S3C2410_LCDCON3_HOZVAL(239) |
				  S3C2410_LCDCON3_HFPD(7),

		.lcdcon4	= S3C2410_LCDCON4_MVAL(0) |
				  S3C2410_LCDCON4_HSPW(3),

		.lcdcon5	= S3C2410_LCDCON5_FRM565 |
				  S3C2410_LCDCON5_INVVLINE |
				  S3C2410_LCDCON5_INVVFRAME |
				  S3C2410_LCDCON5_PWREN |
				  S3C2410_LCDCON5_HWSWP,
	},

#if 0
	/* currently setup by downloader */
	.gpccon		= 0xaa940659,
	.gpccon_mask	= 0xffffffff,
	.gpcup		= 0x0000ffff,
	.gpcup_mask	= 0xffffffff,
	.gpdcon		= 0xaa84aaa0,
	.gpdcon_mask	= 0xffffffff,
	.gpdup		= 0x0000faff,
	.gpdup_mask	= 0xffffffff,
#endif

	.lpcsel		= ((0xCE6) & ~7) | 1<<4,

	.width		= 240,
	.height		= 320,

	.xres		= {
		.min	= 240,
		.max	= 240,
		.defval	= 240,
	},

	.yres		= {
		.min	= 320,
		.max	= 320,
		.defval = 320,
	},

	.bpp		= {
		.min	= 16,
		.max	= 16,
		.defval = 16,
	},
};

static struct platform_device *smdk2440_devices[] __initdata = {
	&s3c_device_usb,
	&s3c_device_lcd,
	&s3c_device_wdt,
	&s3c_device_i2c,
	&s3c_device_iis,
};

static struct s3c24xx_board smdk2440_board __initdata = {
	.devices       = smdk2440_devices,
	.devices_count = ARRAY_SIZE(smdk2440_devices)
};

static void __init smdk2440_map_io(void)
{
	s3c24xx_init_io(smdk2440_iodesc, ARRAY_SIZE(smdk2440_iodesc));
	s3c24xx_init_clocks(16934400);
	s3c24xx_init_uarts(smdk2440_uartcfgs, ARRAY_SIZE(smdk2440_uartcfgs));
	s3c24xx_set_board(&smdk2440_board);
}

static void __init smdk2440_machine_init(void)
{
	/* Configure the LEDs (even if we have no LED support)*/

	s3c2410_gpio_cfgpin(S3C2410_GPF4, S3C2410_GPF4_OUTP);
	s3c2410_gpio_cfgpin(S3C2410_GPF5, S3C2410_GPF5_OUTP);
	s3c2410_gpio_cfgpin(S3C2410_GPF6, S3C2410_GPF6_OUTP);
	s3c2410_gpio_cfgpin(S3C2410_GPF7, S3C2410_GPF7_OUTP);

	s3c2410_gpio_setpin(S3C2410_GPF4, 0);
	s3c2410_gpio_setpin(S3C2410_GPF5, 0);
	s3c2410_gpio_setpin(S3C2410_GPF6, 0);
	s3c2410_gpio_setpin(S3C2410_GPF7, 0);

	s3c24xx_fb_set_platdata(&smdk2440_lcd_cfg);

	s3c2410_pm_init();
}

MACHINE_START(S3C2440, "SMDK2440")
	/* Maintainer: Ben Dooks <ben@fluff.org> */
	.phys_ram	= S3C2410_SDRAM_PA,
	.phys_io	= S3C2410_PA_UART,
	.io_pg_offst	= (((u32)S3C24XX_VA_UART) >> 18) & 0xfffc,
	.boot_params	= S3C2410_SDRAM_PA + 0x100,

	.init_irq	= s3c24xx_init_irq,
	.map_io		= smdk2440_map_io,
	.init_machine	= smdk2440_machine_init,
	.timer		= &s3c24xx_timer,
MACHINE_END
