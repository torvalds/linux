// SPDX-License-Identifier: GPL-2.0+
//
// Copyright (c) 2007 TinCanTools
//	David Anders <danders@amltd.com>
//
// @History:
// derived from linux/arch/arm/mach-s3c2410/mach-bast.c, written by
// Ben Dooks <ben@simtec.co.uk>

#include <linux/gpio/machine.h>
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/interrupt.h>
#include <linux/list.h>
#include <linux/timer.h>
#include <linux/init.h>
#include <linux/device.h>
#include <linux/platform_device.h>
#include <linux/serial_core.h>
#include <linux/serial_s3c.h>
#include <linux/io.h>

#include <asm/mach/arch.h>
#include <asm/mach/map.h>
#include <asm/mach/irq.h>
#include <asm/mach/flash.h>

#include <asm/irq.h>
#include <asm/mach-types.h>

#include <linux/platform_data/i2c-s3c2410.h>
#include "devs.h"
#include "cpu.h"

#include <linux/mtd/mtd.h>
#include <linux/mtd/partitions.h>
#include <linux/mtd/map.h>
#include <linux/mtd/physmap.h>

#include "s3c24xx.h"

static struct resource tct_hammer_nor_resource =
			DEFINE_RES_MEM(0x00000000, SZ_16M);

static struct mtd_partition tct_hammer_mtd_partitions[] = {
	{
		.name		= "System",
		.size		= 0x240000,
		.offset		= 0,
		.mask_flags 	= MTD_WRITEABLE,  /* force read-only */
	}, {
		.name		= "JFFS2",
		.size		= MTDPART_SIZ_FULL,
		.offset		= MTDPART_OFS_APPEND,
	}
};

static struct physmap_flash_data tct_hammer_flash_data = {
	.width		= 2,
	.parts		= tct_hammer_mtd_partitions,
	.nr_parts	= ARRAY_SIZE(tct_hammer_mtd_partitions),
};

static struct platform_device tct_hammer_device_nor = {
	.name		= "physmap-flash",
	.id		= 0,
	.dev = {
			.platform_data = &tct_hammer_flash_data,
		},
	.num_resources	= 1,
	.resource	= &tct_hammer_nor_resource,
};

static struct map_desc tct_hammer_iodesc[] __initdata = {
};

#define UCON S3C2410_UCON_DEFAULT
#define ULCON S3C2410_LCON_CS8 | S3C2410_LCON_PNONE | S3C2410_LCON_STOPB
#define UFCON S3C2410_UFCON_RXTRIG8 | S3C2410_UFCON_FIFOMODE

static struct s3c2410_uartcfg tct_hammer_uartcfgs[] = {
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

static struct gpiod_lookup_table tct_hammer_mmc_gpio_table = {
	.dev_id = "s3c2410-sdi",
	.table = {
		/* bus pins */
		GPIO_LOOKUP_IDX("GPIOE",  5, "bus", 0, GPIO_ACTIVE_HIGH),
		GPIO_LOOKUP_IDX("GPIOE",  6, "bus", 1, GPIO_ACTIVE_HIGH),
		GPIO_LOOKUP_IDX("GPIOE",  7, "bus", 2, GPIO_ACTIVE_HIGH),
		GPIO_LOOKUP_IDX("GPIOE",  8, "bus", 3, GPIO_ACTIVE_HIGH),
		GPIO_LOOKUP_IDX("GPIOE",  9, "bus", 4, GPIO_ACTIVE_HIGH),
		GPIO_LOOKUP_IDX("GPIOE", 10, "bus", 5, GPIO_ACTIVE_HIGH),
		{ },
	},
};

static struct platform_device *tct_hammer_devices[] __initdata = {
	&s3c_device_adc,
	&s3c_device_wdt,
	&s3c_device_i2c0,
	&s3c_device_ohci,
	&s3c_device_rtc,
	&s3c_device_usbgadget,
	&s3c_device_sdi,
	&tct_hammer_device_nor,
};

static void __init tct_hammer_map_io(void)
{
	s3c24xx_init_io(tct_hammer_iodesc, ARRAY_SIZE(tct_hammer_iodesc));
	s3c24xx_init_uarts(tct_hammer_uartcfgs, ARRAY_SIZE(tct_hammer_uartcfgs));
	s3c24xx_set_timer_source(S3C24XX_PWM3, S3C24XX_PWM4);
}

static void __init tct_hammer_init_time(void)
{
	s3c2410_init_clocks(12000000);
	s3c24xx_timer_init();
}

static void __init tct_hammer_init(void)
{
	s3c_i2c0_set_platdata(NULL);
	gpiod_add_lookup_table(&tct_hammer_mmc_gpio_table);
	platform_add_devices(tct_hammer_devices, ARRAY_SIZE(tct_hammer_devices));
}

MACHINE_START(TCT_HAMMER, "TCT_HAMMER")
	.atag_offset	= 0x100,
	.map_io		= tct_hammer_map_io,
	.init_irq	= s3c2410_init_irq,
	.init_machine	= tct_hammer_init,
	.init_time	= tct_hammer_init_time,
MACHINE_END
