// SPDX-License-Identifier: GPL-2.0
//
// Copyright (C) 2008-2009 Samsung Electronics

#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/interrupt.h>
#include <linux/list.h>
#include <linux/timer.h>
#include <linux/init.h>
#include <linux/serial_core.h>
#include <linux/serial_s3c.h>
#include <linux/platform_device.h>
#include <linux/io.h>
#include <linux/i2c.h>
#include <linux/fb.h>
#include <linux/gpio.h>
#include <linux/delay.h>

#include <video/platform_lcd.h>
#include <video/samsung_fimd.h>

#include <asm/mach/arch.h>
#include <asm/mach/map.h>
#include <asm/mach/irq.h>

#include "irqs.h"
#include "map.h"

#include <asm/irq.h>
#include <asm/mach-types.h>

#include <linux/platform_data/i2c-s3c2410.h>
#include "fb.h"

#include "devs.h"
#include "cpu.h"

#include "s3c64xx.h"

#define UCON S3C2410_UCON_DEFAULT
#define ULCON S3C2410_LCON_CS8 | S3C2410_LCON_PNONE
#define UFCON S3C2410_UFCON_RXTRIG8 | S3C2410_UFCON_FIFOMODE

static struct s3c2410_uartcfg ncp_uartcfgs[] __initdata = {
	/* REVISIT: NCP uses only serial 1, 2 */
	[0] = {
		.hwport	     = 0,
		.flags	     = 0,
		.ucon	     = UCON,
		.ulcon	     = ULCON,
		.ufcon	     = UFCON,
	},
	[1] = {
		.hwport	     = 1,
		.flags	     = 0,
		.ucon	     = UCON,
		.ulcon	     = ULCON,
		.ufcon	     = UFCON,
	},
	[2] = {
		.hwport	     = 2,
		.flags	     = 0,
		.ucon	     = UCON,
		.ulcon	     = ULCON,
		.ufcon	     = UFCON,
	},
};

static struct platform_device *ncp_devices[] __initdata = {
	&s3c_device_hsmmc1,
	&s3c_device_i2c0,
};

static struct map_desc ncp_iodesc[] __initdata = {};

static void __init ncp_map_io(void)
{
	s3c64xx_init_io(ncp_iodesc, ARRAY_SIZE(ncp_iodesc));
	s3c64xx_set_xtal_freq(12000000);
	s3c24xx_init_uarts(ncp_uartcfgs, ARRAY_SIZE(ncp_uartcfgs));
	s3c64xx_set_timer_source(S3C64XX_PWM3, S3C64XX_PWM4);
}

static void __init ncp_machine_init(void)
{
	s3c_i2c0_set_platdata(NULL);

	platform_add_devices(ncp_devices, ARRAY_SIZE(ncp_devices));
}

MACHINE_START(NCP, "NCP")
	/* Maintainer: Samsung Electronics */
	.atag_offset	= 0x100,
	.nr_irqs	= S3C64XX_NR_IRQS,
	.init_irq	= s3c6410_init_irq,
	.map_io		= ncp_map_io,
	.init_machine	= ncp_machine_init,
	.init_time	= s3c64xx_timer_init,
MACHINE_END
