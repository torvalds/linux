/* linux/arch/arm/mach-s3c2410/mach-rx3715.c
 *
 * Copyright (c) 2003,2004 Simtec Electronics
 *	Ben Dooks <ben@simtec.co.uk>
 *
 * http://www.handhelds.org/projects/rx3715.html
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * Modifications:
 *	16-Sep-2004 BJD  Copied from mach-h1940.c
 *	25-Oct-2004 BJD  Updates for 2.6.10-rc1
 *	10-Jan-2005 BJD  Removed include of s3c2410.h s3c2440.h
 *	14-Jan-2005 BJD  Added new clock init
 *	10-Mar-2005 LCVR Changed S3C2410_VA to S3C24XX_VA
 *	14-Mar-2005 BJD  Fixed __iomem warnings
 *	20-Sep-2005 BJD  Added static to non-exported items
 *	31-Oct-2005 BJD  Added LCD setup for framebuffer
*/

#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/interrupt.h>
#include <linux/list.h>
#include <linux/timer.h>
#include <linux/init.h>
#include <linux/tty.h>
#include <linux/console.h>
#include <linux/platform_device.h>
#include <linux/serial_core.h>
#include <linux/serial.h>

#include <asm/mach/arch.h>
#include <asm/mach/map.h>
#include <asm/mach/irq.h>

#include <asm/hardware.h>
#include <asm/hardware/iomd.h>
#include <asm/io.h>
#include <asm/irq.h>
#include <asm/mach-types.h>

#include <asm/arch/regs-serial.h>
#include <asm/arch/regs-gpio.h>
#include <asm/arch/regs-lcd.h>

#include <asm/arch/fb.h>

#include "clock.h"
#include "devs.h"
#include "cpu.h"
#include "pm.h"

static struct map_desc rx3715_iodesc[] __initdata = {
	/* dump ISA space somewhere unused */

	{ (u32)S3C24XX_VA_ISA_WORD, S3C2410_CS3, SZ_16M, MT_DEVICE },
	{ (u32)S3C24XX_VA_ISA_BYTE, S3C2410_CS3, SZ_16M, MT_DEVICE },
};


static struct s3c24xx_uart_clksrc rx3715_serial_clocks[] = {
	[0] = {
		.name		= "fclk",
		.divisor	= 0,
		.min_baud	= 0,
		.max_baud	= 0,
	}
};

static struct s3c2410_uartcfg rx3715_uartcfgs[] = {
	[0] = {
		.hwport	     = 0,
		.flags	     = 0,
		.ucon	     = 0x3c5,
		.ulcon	     = 0x03,
		.ufcon	     = 0x51,
		.clocks	     = rx3715_serial_clocks,
		.clocks_size = ARRAY_SIZE(rx3715_serial_clocks),
	},
	[1] = {
		.hwport	     = 1,
		.flags	     = 0,
		.ucon	     = 0x3c5,
		.ulcon	     = 0x03,
		.ufcon	     = 0x00,
		.clocks	     = rx3715_serial_clocks,
		.clocks_size = ARRAY_SIZE(rx3715_serial_clocks),
	},
	/* IR port */
	[2] = {
		.hwport	     = 2,
		.uart_flags  = UPF_CONS_FLOW,
		.ucon	     = 0x3c5,
		.ulcon	     = 0x43,
		.ufcon	     = 0x51,
		.clocks	     = rx3715_serial_clocks,
		.clocks_size = ARRAY_SIZE(rx3715_serial_clocks),
	}
};

/* framebuffer lcd controller information */

static struct s3c2410fb_mach_info rx3715_lcdcfg __initdata = {
	.regs	= {
		.lcdcon1 =	S3C2410_LCDCON1_TFT16BPP | \
				S3C2410_LCDCON1_TFT | \
				S3C2410_LCDCON1_CLKVAL(0x0C),

		.lcdcon2 =	S3C2410_LCDCON2_VBPD(5) | \
				S3C2410_LCDCON2_LINEVAL(319) | \
				S3C2410_LCDCON2_VFPD(6) | \
				S3C2410_LCDCON2_VSPW(2),

		.lcdcon3 =	S3C2410_LCDCON3_HBPD(35) | \
				S3C2410_LCDCON3_HOZVAL(239) | \
				S3C2410_LCDCON3_HFPD(35),

		.lcdcon4 =	S3C2410_LCDCON4_MVAL(0) | \
				S3C2410_LCDCON4_HSPW(7),

		.lcdcon5 =	S3C2410_LCDCON5_INVVLINE |
				S3C2410_LCDCON5_FRM565 |
				S3C2410_LCDCON5_HWSWP,
	},

	.lpcsel =	0xf82,

	.gpccon =	0xaa955699,
	.gpccon_mask =	0xffc003cc,
	.gpcup =	0x0000ffff,
	.gpcup_mask =	0xffffffff,

	.gpdcon =	0xaa95aaa1,
	.gpdcon_mask =	0xffc0fff0,
	.gpdup =	0x0000faff,
	.gpdup_mask =	0xffffffff,

	.fixed_syncs =	1,
	.width  =	240,
	.height =	320,

	.xres	= {
		.min =		240,
		.max =		240,
		.defval =	240,
	},

	.yres	= {
		.max =		320,
		.min =		320,
		.defval	=	320,
	},

	.bpp	= {
		.min =		16,
		.max =		16,
		.defval =	16,
	},
};

static struct platform_device *rx3715_devices[] __initdata = {
	&s3c_device_usb,
	&s3c_device_lcd,
	&s3c_device_wdt,
	&s3c_device_i2c,
	&s3c_device_iis,
};

static struct s3c24xx_board rx3715_board __initdata = {
	.devices       = rx3715_devices,
	.devices_count = ARRAY_SIZE(rx3715_devices)
};

static void __init rx3715_map_io(void)
{
	s3c24xx_init_io(rx3715_iodesc, ARRAY_SIZE(rx3715_iodesc));
	s3c24xx_init_clocks(16934000);
	s3c24xx_init_uarts(rx3715_uartcfgs, ARRAY_SIZE(rx3715_uartcfgs));
	s3c24xx_set_board(&rx3715_board);
}

static void __init rx3715_init_irq(void)
{
	s3c24xx_init_irq();
}

static void __init rx3715_init_machine(void)
{
	s3c2410_pm_init();
	s3c24xx_fb_set_platdata(&rx3715_lcdcfg);
}


MACHINE_START(RX3715, "IPAQ-RX3715")
	/* Maintainer: Ben Dooks <ben@fluff.org> */
	.phys_ram	= S3C2410_SDRAM_PA,
	.phys_io	= S3C2410_PA_UART,
	.io_pg_offst	= (((u32)S3C24XX_VA_UART) >> 18) & 0xfffc,
	.boot_params	= S3C2410_SDRAM_PA + 0x100,
	.map_io		= rx3715_map_io,
	.init_irq	= rx3715_init_irq,
	.init_machine	= rx3715_init_machine,
	.timer		= &s3c24xx_timer,
MACHINE_END
