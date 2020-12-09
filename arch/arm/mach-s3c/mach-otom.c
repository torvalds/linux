// SPDX-License-Identifier: GPL-2.0
//
// Copyright (c) 2004 Nex Vision
//   Guillaume GOURAT <guillaume.gourat@nexvision.fr>

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

#include <linux/platform_data/i2c-s3c2410.h>

#include <asm/irq.h>
#include <asm/mach-types.h>
#include <asm/mach/arch.h>
#include <asm/mach/map.h>
#include <asm/mach/irq.h>

#include "gpio-samsung.h"
#include "gpio-cfg.h"

#include "cpu.h"
#include "devs.h"

#include "s3c24xx.h"
#include "otom.h"

static struct map_desc otom11_iodesc[] __initdata = {
  /* Device area */
	{ (u32)OTOM_VA_CS8900A_BASE, OTOM_PA_CS8900A_BASE, SZ_16M, MT_DEVICE },
};

#define UCON S3C2410_UCON_DEFAULT
#define ULCON S3C2410_LCON_CS8 | S3C2410_LCON_PNONE | S3C2410_LCON_STOPB
#define UFCON S3C2410_UFCON_RXTRIG12 | S3C2410_UFCON_FIFOMODE

static struct s3c2410_uartcfg otom11_uartcfgs[] __initdata = {
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
	/* port 2 is not actually used */
	[2] = {
		.hwport	     = 2,
		.flags	     = 0,
		.ucon	     = UCON,
		.ulcon	     = ULCON,
		.ufcon	     = UFCON,
	}
};

/* NOR Flash on NexVision OTOM board */

static struct resource otom_nor_resource[] = {
	[0] = DEFINE_RES_MEM(S3C2410_CS0, SZ_4M),
};

static struct platform_device otom_device_nor = {
	.name		= "mtd-flash",
	.id		= -1,
	.num_resources	= ARRAY_SIZE(otom_nor_resource),
	.resource	= otom_nor_resource,
};

/* Standard OTOM devices */

static struct platform_device *otom11_devices[] __initdata = {
	&s3c_device_ohci,
	&s3c_device_lcd,
	&s3c_device_wdt,
	&s3c_device_i2c0,
	&s3c_device_iis,
 	&s3c_device_rtc,
	&otom_device_nor,
};

static void __init otom11_map_io(void)
{
	s3c24xx_init_io(otom11_iodesc, ARRAY_SIZE(otom11_iodesc));
	s3c24xx_init_uarts(otom11_uartcfgs, ARRAY_SIZE(otom11_uartcfgs));
	s3c24xx_set_timer_source(S3C24XX_PWM3, S3C24XX_PWM4);
}

static void __init otom11_init_time(void)
{
	s3c2410_init_clocks(12000000);
	s3c24xx_timer_init();
}

static void __init otom11_init(void)
{
	s3c_i2c0_set_platdata(NULL);

	/* Configure the I2S pins (GPE0...GPE4) in correct mode */
	s3c_gpio_cfgall_range(S3C2410_GPE(0), 5, S3C_GPIO_SFN(2),
			      S3C_GPIO_PULL_NONE);
	platform_add_devices(otom11_devices, ARRAY_SIZE(otom11_devices));
}

MACHINE_START(OTOM, "Nex Vision - Otom 1.1")
	/* Maintainer: Guillaume GOURAT <guillaume.gourat@nexvision.tv> */
	.atag_offset	= 0x100,
	.map_io		= otom11_map_io,
	.init_machine	= otom11_init,
	.init_irq	= s3c2410_init_irq,
	.init_time	= otom11_init_time,
MACHINE_END
