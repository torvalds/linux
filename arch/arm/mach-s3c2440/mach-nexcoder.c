/* linux/arch/arm/mach-s3c2440/mach-nexcoder.c
 *
 * Copyright (c) 2004 Nex Vision
 *   Guillaume GOURAT <guillaume.gourat@nexvision.tv>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * Modifications:
 *     15-10-2004 GG  Created initial version
 *     12-03-2005 BJD Updated for release
 */

#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/interrupt.h>
#include <linux/list.h>
#include <linux/timer.h>
#include <linux/init.h>
#include <linux/string.h>
#include <linux/serial_core.h>
#include <linux/platform_device.h>
#include <linux/io.h>

#include <linux/mtd/map.h>

#include <asm/mach/arch.h>
#include <asm/mach/map.h>
#include <asm/mach/irq.h>

#include <asm/setup.h>
#include <mach/hardware.h>
#include <asm/irq.h>
#include <asm/mach-types.h>

//#include <asm/debug-ll.h>
#include <mach/regs-gpio.h>
#include <asm/plat-s3c/regs-serial.h>

#include <asm/plat-s3c24xx/s3c2410.h>
#include <asm/plat-s3c24xx/s3c2440.h>
#include <asm/plat-s3c24xx/clock.h>
#include <asm/plat-s3c24xx/devs.h>
#include <asm/plat-s3c24xx/cpu.h>

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
	[0] = {
		.start = S3C2410_CS0,
		.end   = S3C2410_CS0 + (8*1024*1024) - 1,
		.flags = IORESOURCE_MEM,
	}
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
	&s3c_device_usb,
	&s3c_device_lcd,
	&s3c_device_wdt,
	&s3c_device_i2c,
	&s3c_device_iis,
 	&s3c_device_rtc,
	&s3c_device_camif,
	&s3c_device_spi0,
	&s3c_device_spi1,
	&nexcoder_device_nor,
};

static void __init nexcoder_sensorboard_init(void)
{
	// Initialize SCCB bus
	s3c2410_gpio_setpin(S3C2410_GPE14, 1); // IICSCL
	s3c2410_gpio_cfgpin(S3C2410_GPE14, S3C2410_GPE14_OUTP);
	s3c2410_gpio_setpin(S3C2410_GPE15, 1); // IICSDA
	s3c2410_gpio_cfgpin(S3C2410_GPE15, S3C2410_GPE15_OUTP);

	// Power up the sensor board
	s3c2410_gpio_setpin(S3C2410_GPF1, 1);
	s3c2410_gpio_cfgpin(S3C2410_GPF1, S3C2410_GPF1_OUTP); // CAM_GPIO7 => nLDO_PWRDN
	s3c2410_gpio_setpin(S3C2410_GPF2, 0);
	s3c2410_gpio_cfgpin(S3C2410_GPF2, S3C2410_GPF2_OUTP); // CAM_GPIO6 => CAM_PWRDN
}

static void __init nexcoder_map_io(void)
{
	s3c24xx_init_io(nexcoder_iodesc, ARRAY_SIZE(nexcoder_iodesc));
	s3c24xx_init_clocks(0);
	s3c24xx_init_uarts(nexcoder_uartcfgs, ARRAY_SIZE(nexcoder_uartcfgs));

	nexcoder_sensorboard_init();
}

static void __init nexcoder_init(void)
{
	platform_add_devices(nexcoder_devices, ARRAY_SIZE(nexcoder_devices));
};

MACHINE_START(NEXCODER_2440, "NexVision - Nexcoder 2440")
	/* Maintainer: Guillaume GOURAT <guillaume.gourat@nexvision.tv> */
	.phys_io	= S3C2410_PA_UART,
	.io_pg_offst	= (((u32)S3C24XX_VA_UART) >> 18) & 0xfffc,
	.boot_params	= S3C2410_SDRAM_PA + 0x100,
	.map_io		= nexcoder_map_io,
	.init_machine	= nexcoder_init,
	.init_irq	= s3c24xx_init_irq,
	.timer		= &s3c24xx_timer,
MACHINE_END
