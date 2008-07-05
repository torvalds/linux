/*
 * Renesas - AP-325RXA
 * (Compatible with Algo System ., LTD. - AP-320A)
 *
 * Copyright (C) 2008 Renesas Solutions Corp.
 * Author : Yusuke Goda <goda.yuske@renesas.com>
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 */

#include <linux/init.h>
#include <linux/device.h>
#include <linux/platform_device.h>
#include <linux/mtd/physmap.h>
#include <linux/delay.h>
#include <linux/i2c.h>
#include <asm/io.h>

static struct resource smc9118_resources[] = {
	[0] = {
		.start	= 0xb6080000,
		.end	= 0xb60fffff,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= 35,
		.end	= 35,
		.flags	= IORESOURCE_IRQ,
	}
};

static struct platform_device smc9118_device = {
	.name		= "smc911x",
	.id		= -1,
	.num_resources	= ARRAY_SIZE(smc9118_resources),
	.resource	= smc9118_resources,
};

static struct mtd_partition ap325rxa_nor_flash_partitions[] = {
	{
		 .name = "uboot",
		 .offset = 0,
		 .size = (1 * 1024 * 1024),
		 .mask_flags = MTD_WRITEABLE,	/* Read-only */
	}, {
		 .name = "kernel",
		 .offset = MTDPART_OFS_APPEND,
		 .size = (2 * 1024 * 1024),
	}, {
		 .name = "other",
		 .offset = MTDPART_OFS_APPEND,
		 .size = MTDPART_SIZ_FULL,
	},
};

static struct physmap_flash_data ap325rxa_nor_flash_data = {
	.width		= 2,
	.parts		= ap325rxa_nor_flash_partitions,
	.nr_parts	= ARRAY_SIZE(ap325rxa_nor_flash_partitions),
};

static struct resource ap325rxa_nor_flash_resources[] = {
	[0] = {
		.name	= "NOR Flash",
		.start	= 0x00000000,
		.end	= 0x00ffffff,
		.flags	= IORESOURCE_MEM,
	}
};

static struct platform_device ap325rxa_nor_flash_device = {
	.name		= "physmap-flash",
	.resource	= ap325rxa_nor_flash_resources,
	.num_resources	= ARRAY_SIZE(ap325rxa_nor_flash_resources),
	.dev		= {
		.platform_data = &ap325rxa_nor_flash_data,
	},
};

static struct platform_device *ap325rxa_devices[] __initdata = {
	&smc9118_device,
	&ap325rxa_nor_flash_device
};

static struct i2c_board_info __initdata ap325rxa_i2c_devices[] = {
};

static int __init ap325rxa_devices_setup(void)
{
	i2c_register_board_info(0, ap325rxa_i2c_devices,
				ARRAY_SIZE(ap325rxa_i2c_devices));
 
	return platform_add_devices(ap325rxa_devices,
				ARRAY_SIZE(ap325rxa_devices));
}
device_initcall(ap325rxa_devices_setup);

#define MSTPCR0		(0xA4150030)
#define MSTPCR1		(0xA4150034)
#define MSTPCR2		(0xA4150038)

static void __init ap325rxa_setup(char **cmdline_p)
{
	/* enable VEU0 + VEU1 */
	ctrl_outl(ctrl_inl(MSTPCR2) & ~0x00000044, MSTPCR2);	/* bit 2 + 6 */

	/* enable MERAM */
	ctrl_outl(ctrl_inl(MSTPCR0) & ~0x00000001, MSTPCR0);	/* bit 0 */

	/* I2C */
	ctrl_outl(ctrl_inl(MSTPCR1) & ~0x00000200, MSTPCR1);
}

static struct sh_machine_vector mv_ap325rxa __initmv = {
	.mv_name = "AP-325RXA",
	.mv_setup = ap325rxa_setup,
};
