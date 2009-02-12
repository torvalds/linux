/*
 * linux/arch/arm/mach-w90x900/mach-w90p910evb.c
 *
 * Based on mach-s3c2410/mach-smdk2410.c by Jonas Dietsche
 *
 * Copyright (C) 2008 Nuvoton technology corporation
 * All rights reserved.
 *
 * Wan ZongShun <mcuos.com@gmail.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 *
 */

#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/interrupt.h>
#include <linux/list.h>
#include <linux/timer.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/mtd/physmap.h>

#include <asm/mach/arch.h>
#include <asm/mach/map.h>
#include <asm/mach/irq.h>
#include <asm/mach-types.h>

#include <mach/regs-serial.h>
#include <mach/map.h>

#include "cpu.h"
/*w90p910 evb norflash driver data */

#define W90P910_FLASH_BASE	0xA0000000
#define W90P910_FLASH_SIZE	0x400000

static struct mtd_partition w90p910_flash_partitions[] = {
	{
		.name	=	"NOR Partition 1 for kernel (960K)",
		.size	=	0xF0000,
		.offset	=	0x10000,
	},
	{
		.name	=	"NOR Partition 2 for image (1M)",
		.size	=	0x100000,
		.offset	=	0x100000,
	},
	{
		.name	=	"NOR Partition 3 for user (2M)",
		.size	=	0x200000,
		.offset	=	0x00200000,
	}
};

static struct physmap_flash_data w90p910_flash_data = {
	.width		=	2,
	.parts		=	w90p910_flash_partitions,
	.nr_parts	=	ARRAY_SIZE(w90p910_flash_partitions),
};

static struct resource w90p910_flash_resources[] = {
	{
		.start	=	W90P910_FLASH_BASE,
		.end	=	W90P910_FLASH_BASE + W90P910_FLASH_SIZE - 1,
		.flags	=	IORESOURCE_MEM,
	}
};

static struct platform_device w90p910_flash_device = {
	.name		=	"physmap-flash",
	.id		=	0,
	.dev		= {
				.platform_data = &w90p910_flash_data,
			},
	.resource	=	w90p910_flash_resources,
	.num_resources	=	ARRAY_SIZE(w90p910_flash_resources),
};

static struct map_desc w90p910_iodesc[] __initdata = {
};

/*Here should be your evb resourse,such as LCD*/

static struct platform_device *w90p910evb_dev[] __initdata = {
	&w90p910_serial_device,
	&w90p910_flash_device,
};

static void __init w90p910evb_map_io(void)
{
	w90p910_map_io(w90p910_iodesc, ARRAY_SIZE(w90p910_iodesc));
	w90p910_init_clocks(0);
}

static void __init w90p910evb_init(void)
{
	platform_add_devices(w90p910evb_dev, ARRAY_SIZE(w90p910evb_dev));
}

MACHINE_START(W90P910EVB, "W90P910EVB")
	/* Maintainer: Wan ZongShun */
	.phys_io	= W90X900_PA_UART,
	.io_pg_offst	= (((u32)W90X900_VA_UART) >> 18) & 0xfffc,
	.boot_params	= 0,
	.map_io		= w90p910evb_map_io,
	.init_irq	= w90x900_init_irq,
	.init_machine	= w90p910evb_init,
	.timer		= &w90x900_timer,
MACHINE_END
