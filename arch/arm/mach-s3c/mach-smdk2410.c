// SPDX-License-Identifier: GPL-2.0+
//
// Copyright (C) 2004 by FS Forth-Systeme GmbH
// All rights reserved.
//
// @Author: Jonas Dietsche
//
// @History:
// derived from linux/arch/arm/mach-s3c2410/mach-bast.c, written by
// Ben Dooks <ben@simtec.co.uk>

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
#include "gpio-samsung.h"
#include "gpio-cfg.h"

#include <asm/mach/arch.h>
#include <asm/mach/map.h>
#include <asm/mach/irq.h>

#include <asm/irq.h>
#include <asm/mach-types.h>

#include <linux/platform_data/i2c-s3c2410.h>

#include "devs.h"
#include "cpu.h"

#include "s3c24xx.h"
#include "common-smdk-s3c24xx.h"

static struct map_desc smdk2410_iodesc[] __initdata = {
  /* nothing here yet */
};

#define UCON S3C2410_UCON_DEFAULT
#define ULCON S3C2410_LCON_CS8 | S3C2410_LCON_PNONE | S3C2410_LCON_STOPB
#define UFCON S3C2410_UFCON_RXTRIG8 | S3C2410_UFCON_FIFOMODE

static struct s3c2410_uartcfg smdk2410_uartcfgs[] __initdata = {
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
	}
};

static struct platform_device *smdk2410_devices[] __initdata = {
	&s3c_device_ohci,
	&s3c_device_lcd,
	&s3c_device_wdt,
	&s3c_device_i2c0,
	&s3c_device_iis,
};

static void __init smdk2410_map_io(void)
{
	s3c24xx_init_io(smdk2410_iodesc, ARRAY_SIZE(smdk2410_iodesc));
	s3c24xx_init_uarts(smdk2410_uartcfgs, ARRAY_SIZE(smdk2410_uartcfgs));
	s3c24xx_set_timer_source(S3C24XX_PWM3, S3C24XX_PWM4);
}

static void __init smdk2410_init_time(void)
{
	s3c2410_init_clocks(12000000);
	s3c24xx_timer_init();
}

static void __init smdk2410_init(void)
{
	s3c_i2c0_set_platdata(NULL);
	platform_add_devices(smdk2410_devices, ARRAY_SIZE(smdk2410_devices));
	/* Configure the I2S pins (GPE0...GPE4) in correct mode */
	s3c_gpio_cfgall_range(S3C2410_GPE(0), 5, S3C_GPIO_SFN(2),
			      S3C_GPIO_PULL_NONE);
	smdk_machine_init();
}

MACHINE_START(SMDK2410, "SMDK2410") /* @TODO: request a new identifier and switch
				    * to SMDK2410 */
	/* Maintainer: Jonas Dietsche */
	.atag_offset	= 0x100,
	.nr_irqs	= NR_IRQS_S3C2410,
	.map_io		= smdk2410_map_io,
	.init_irq	= s3c2410_init_irq,
	.init_machine	= smdk2410_init,
	.init_time	= smdk2410_init_time,
MACHINE_END
