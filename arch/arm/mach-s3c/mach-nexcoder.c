// SPDX-License-Identifier: GPL-2.0
// linux/arch/arm/mach-s3c2440/mach-nexcoder.c
//
// Copyright (c) 2004 Nex Vision
//   Guillaume GOURAT <guillaume.gourat@nexvision.tv>
//
// Modifications:
//     15-10-2004 GG  Created initial version
//     12-03-2005 BJD Updated for release

#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/interrupt.h>
#include <linux/list.h>
#include <linux/timer.h>
#include <linux/init.h>
#include <linux/gpio.h>
#include <linux/string.h>
#include <linux/serial_core.h>
#include <linux/serial_s3c.h>
#include <linux/platform_device.h>
#include <linux/io.h>

#include <linux/mtd/map.h>

#include <asm/mach/arch.h>
#include <asm/mach/map.h>
#include <asm/mach/irq.h>

#include <asm/setup.h>
#include <asm/irq.h>
#include <asm/mach-types.h>

//#include <asm/debug-ll.h>
#include "regs-gpio.h"
#include "gpio-samsung.h"
#include <linux/platform_data/i2c-s3c2410.h>

#include "gpio-cfg.h"
#include "devs.h"
#include "cpu.h"

#include "s3c24xx.h"

static struct map_desc nexcoder_iodesc[] __initdata = {
	/* nothing here yet */
};

#define UCON S3C2410_UCON_DEFAULT
#define ULCON S3C2410_LCON_CS8 | S3C2410_LCON_PNONE | S3C2410_LCON_STOPB
#define UFCON S3C2410_UFCON_RXTRIG12 | S3C2410_UFCON_FIFOMODE

static struct s3c2410_uartcfg nexcoder_uartcfgs[] __initdata = {
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

/* NOR Flash on NexVision NexCoder 2440 board */

static struct resource nexcoder_nor_resource[] = {
	[0] = DEFINE_RES_MEM(S3C2410_CS0, SZ_8M),
};

static struct map_info nexcoder_nor_map = {
	.bankwidth = 2,
};

static struct platform_device nexcoder_device_nor = {
	.name		= "mtd-flash",
	.id		= -1,
	.num_resources	= ARRAY_SIZE(nexcoder_nor_resource),
	.resource	= nexcoder_nor_resource,
	.dev =
	{
		.platform_data = &nexcoder_nor_map,
	}
};

/* Standard Nexcoder devices */

static struct platform_device *nexcoder_devices[] __initdata = {
	&s3c_device_ohci,
	&s3c_device_lcd,
	&s3c_device_wdt,
	&s3c_device_i2c0,
	&s3c_device_iis,
 	&s3c_device_rtc,
	&s3c_device_camif,
	&s3c_device_spi0,
	&s3c_device_spi1,
	&nexcoder_device_nor,
};

static void __init nexcoder_sensorboard_init(void)
{
	/* Initialize SCCB bus */
	gpio_request_one(S3C2410_GPE(14), GPIOF_OUT_INIT_HIGH, NULL);
	gpio_free(S3C2410_GPE(14)); /* IICSCL */
	gpio_request_one(S3C2410_GPE(15), GPIOF_OUT_INIT_HIGH, NULL);
	gpio_free(S3C2410_GPE(15)); /* IICSDA */

	/* Power up the sensor board */
	gpio_request_one(S3C2410_GPF(1), GPIOF_OUT_INIT_HIGH, NULL);
	gpio_free(S3C2410_GPF(1)); /* CAM_GPIO7 => nLDO_PWRDN */
	gpio_request_one(S3C2410_GPF(2), GPIOF_OUT_INIT_LOW, NULL);
	gpio_free(S3C2410_GPF(2)); /* CAM_GPIO6 => CAM_PWRDN */
}

static void __init nexcoder_map_io(void)
{
	s3c24xx_init_io(nexcoder_iodesc, ARRAY_SIZE(nexcoder_iodesc));
	s3c24xx_init_uarts(nexcoder_uartcfgs, ARRAY_SIZE(nexcoder_uartcfgs));
	s3c24xx_set_timer_source(S3C24XX_PWM3, S3C24XX_PWM4);

	nexcoder_sensorboard_init();
}

static void __init nexcoder_init_time(void)
{
	s3c2440_init_clocks(12000000);
	s3c24xx_timer_init();
}

static void __init nexcoder_init(void)
{
	s3c_i2c0_set_platdata(NULL);

	/* Configure the I2S pins (GPE0...GPE4) in correct mode */
	s3c_gpio_cfgall_range(S3C2410_GPE(0), 5, S3C_GPIO_SFN(2),
			      S3C_GPIO_PULL_NONE);

	platform_add_devices(nexcoder_devices, ARRAY_SIZE(nexcoder_devices));
};

MACHINE_START(NEXCODER_2440, "NexVision - Nexcoder 2440")
	/* Maintainer: Guillaume GOURAT <guillaume.gourat@nexvision.tv> */
	.atag_offset	= 0x100,
	.map_io		= nexcoder_map_io,
	.init_machine	= nexcoder_init,
	.init_irq	= s3c2440_init_irq,
	.init_time	= nexcoder_init_time,
MACHINE_END
