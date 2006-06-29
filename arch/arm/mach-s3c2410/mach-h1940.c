/* linux/arch/arm/mach-s3c2410/mach-h1940.c
 *
 * Copyright (c) 2003-2005 Simtec Electronics
 *   Ben Dooks <ben@simtec.co.uk>
 *
 * http://www.handhelds.org/projects/h1940.html
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * Modifications:
 *     16-May-2003 BJD  Created initial version
 *     16-Aug-2003 BJD  Fixed header files and copyright, added URL
 *     05-Sep-2003 BJD  Moved to v2.6 kernel
 *     06-Jan-2003 BJD  Updates for <arch/map.h>
 *     18-Jan-2003 BJD  Added serial port configuration
 *     17-Feb-2003 BJD  Copied to mach-ipaq.c
 *     21-Aug-2004 BJD  Added struct s3c2410_board
 *     04-Sep-2004 BJD  Changed uart init, renamed ipaq_ -> h1940_
 *     18-Oct-2004 BJD  Updated new board structure name
 *     04-Nov-2004 BJD  Change for new serial clock
 *     04-Jan-2005 BJD  Updated uart init call
 *     10-Jan-2005 BJD  Removed include of s3c2410.h
 *     14-Jan-2005 BJD  Added clock init
 *     10-Mar-2005 LCVR Changed S3C2410_VA to S3C24XX_VA
 *     20-Sep-2005 BJD  Added static to non-exported items
 *     26-Oct-2005 BJD  Changed name of fb init call
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


#include <asm/arch/regs-serial.h>
#include <asm/arch/regs-lcd.h>

#include <asm/arch/h1940-latch.h>
#include <asm/arch/fb.h>

#include <linux/serial_core.h>

#include "clock.h"
#include "devs.h"
#include "cpu.h"

static struct map_desc h1940_iodesc[] __initdata = {
	[0] = {
		.virtual	= (unsigned long)H1940_LATCH,
		.pfn		= __phys_to_pfn(H1940_PA_LATCH),
		.length		= SZ_16K,
		.type		= MT_DEVICE
	},
};

#define UCON S3C2410_UCON_DEFAULT | S3C2410_UCON_UCLK
#define ULCON S3C2410_LCON_CS8 | S3C2410_LCON_PNONE | S3C2410_LCON_STOPB
#define UFCON S3C2410_UFCON_RXTRIG8 | S3C2410_UFCON_FIFOMODE

static struct s3c2410_uartcfg h1940_uartcfgs[] __initdata = {
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
		.ucon	     = 0x245,
		.ulcon	     = 0x03,
		.ufcon	     = 0x00,
	},
	/* IR port */
	[2] = {
		.hwport	     = 2,
		.flags	     = 0,
		.uart_flags  = UPF_CONS_FLOW,
		.ucon	     = 0x3c5,
		.ulcon	     = 0x43,
		.ufcon	     = 0x51,
	}
};

/* Board control latch control */

static unsigned int latch_state = H1940_LATCH_DEFAULT;

void h1940_latch_control(unsigned int clear, unsigned int set)
{
	unsigned long flags;

	local_irq_save(flags);

	latch_state &= ~clear;
	latch_state |= set;

	__raw_writel(latch_state, H1940_LATCH);

	local_irq_restore(flags);
}

EXPORT_SYMBOL_GPL(h1940_latch_control);


/**
 * Set lcd on or off
 **/
static struct s3c2410fb_mach_info h1940_lcdcfg __initdata = {
	.fixed_syncs=		1,
	.regs={
		.lcdcon1=	S3C2410_LCDCON1_TFT16BPP | \
				S3C2410_LCDCON1_TFT | \
				S3C2410_LCDCON1_CLKVAL(0x0C),

		.lcdcon2=	S3C2410_LCDCON2_VBPD(7) | \
				S3C2410_LCDCON2_LINEVAL(319) | \
				S3C2410_LCDCON2_VFPD(6) | \
				S3C2410_LCDCON2_VSPW(0),

		.lcdcon3=	S3C2410_LCDCON3_HBPD(19) | \
				S3C2410_LCDCON3_HOZVAL(239) | \
				S3C2410_LCDCON3_HFPD(7),

		.lcdcon4=	S3C2410_LCDCON4_MVAL(0) | \
				S3C2410_LCDCON4_HSPW(3),

		.lcdcon5=	S3C2410_LCDCON5_FRM565 | \
				S3C2410_LCDCON5_INVVLINE | \
				S3C2410_LCDCON5_HWSWP,
	},
	.lpcsel=	0x02,
	.gpccon=	0xaa940659,
	.gpccon_mask=	0xffffffff,
	.gpcup=		0x0000ffff,
	.gpcup_mask=	0xffffffff,
	.gpdcon=	0xaa84aaa0,
	.gpdcon_mask=	0xffffffff,
	.gpdup=		0x0000faff,
	.gpdup_mask=	0xffffffff,

	.width=		240,
	.height=	320,
	.xres=		{240,240,240},
	.yres=		{320,320,320},
	.bpp=		{16,16,16},
};

static struct platform_device *h1940_devices[] __initdata = {
	&s3c_device_usb,
	&s3c_device_lcd,
	&s3c_device_wdt,
	&s3c_device_i2c,
	&s3c_device_iis,
};

static struct s3c24xx_board h1940_board __initdata = {
	.devices       = h1940_devices,
	.devices_count = ARRAY_SIZE(h1940_devices)
};

static void __init h1940_map_io(void)
{
	s3c24xx_init_io(h1940_iodesc, ARRAY_SIZE(h1940_iodesc));
	s3c24xx_init_clocks(0);
	s3c24xx_init_uarts(h1940_uartcfgs, ARRAY_SIZE(h1940_uartcfgs));
	s3c24xx_set_board(&h1940_board);
}

static void __init h1940_init_irq(void)
{
	s3c24xx_init_irq();

}

static void __init h1940_init(void)
{
	s3c24xx_fb_set_platdata(&h1940_lcdcfg);
}

MACHINE_START(H1940, "IPAQ-H1940")
	/* Maintainer: Ben Dooks <ben@fluff.org> */
	.phys_io	= S3C2410_PA_UART,
	.io_pg_offst	= (((u32)S3C24XX_VA_UART) >> 18) & 0xfffc,
	.boot_params	= S3C2410_SDRAM_PA + 0x100,
	.map_io		= h1940_map_io,
	.init_irq	= h1940_init_irq,
	.init_machine   = h1940_init,
	.timer		= &s3c24xx_timer,
MACHINE_END
