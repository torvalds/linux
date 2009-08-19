/*
 * Copyright (C) 2009 Renesas Solutions Corp.
 *
 * Kuninori Morimoto <morimoto.kuninori@renesas.com>
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 */

#include <linux/init.h>
#include <linux/device.h>
#include <linux/platform_device.h>
#include <linux/mtd/physmap.h>
#include <linux/gpio.h>
#include <linux/interrupt.h>
#include <asm/io.h>
#include <asm/heartbeat.h>
#include <cpu/sh7724.h>

/*
 * Area   Address                    Interface     size  BusWidth
 * 0      0x0000_0000 ~ 0x03FF_FFFF  FROM          64MB  16bit
 * 1      0x0400_0000 ~ 0x07FF_FFFF  Internal I/O  64MB  16/32bit
 * 2      0x0800_0000 ~ 0x0BFF_FFFF  DRAM 2        64MB  32bit
 * 3      0x0C00_0000 ~ 0x0FFF_FFFF  DRAM 3        64MB  32bit
 * 4      0x1000_0000 ~ 0x13FF_FFFF  DRAM 4        64MB  32bit
 * 5      0x1400_0000 ~ 0x17FF_FFFF  DRAM 5        64MB  32bit
 * 6      0x1800_0000 ~ 0x1BFF_FFFF  MFI           64MB  16bit
 */

/* Heartbeat */
static unsigned char led_pos[] = { 0, 1, 2, 3 };
static struct heartbeat_data heartbeat_data = {
	.regsize = 8,
	.nr_bits = 4,
	.bit_pos = led_pos,
};

static struct resource heartbeat_resources[] = {
	[0] = {
		.start  = 0xA405012C, /* PTG */
		.end    = 0xA405012E - 1,
		.flags  = IORESOURCE_MEM,
	},
};

static struct platform_device heartbeat_device = {
	.name           = "heartbeat",
	.id             = -1,
	.dev = {
		.platform_data = &heartbeat_data,
	},
	.num_resources  = ARRAY_SIZE(heartbeat_resources),
	.resource       = heartbeat_resources,
};

/* MTD */
static struct mtd_partition nor_flash_partitions[] = {
	{
		.name = "uboot",
		.offset = 0,
		.size = (256 * 1024),
		.mask_flags = MTD_CAP_ROM,
	}, {
		.name = "kernel",
		.offset = MTDPART_OFS_APPEND,
		.size = (2 * 1024 * 1024),
	}, {
		.name = "free-area",
		.offset = MTDPART_OFS_APPEND,
		.size = MTDPART_SIZ_FULL,
	},
};

static struct physmap_flash_data nor_flash_data = {
	.width		= 2,
	.parts		= nor_flash_partitions,
	.nr_parts	= ARRAY_SIZE(nor_flash_partitions),
};

static struct resource nor_flash_resources[] = {
	[0] = {
		.name	= "NOR Flash",
		.start	= 0x00000000,
		.end	= 0x03ffffff,
		.flags	= IORESOURCE_MEM,
	}
};

static struct platform_device nor_flash_device = {
	.name		= "physmap-flash",
	.resource	= nor_flash_resources,
	.num_resources	= ARRAY_SIZE(nor_flash_resources),
	.dev		= {
		.platform_data = &nor_flash_data,
	},
};

static struct platform_device *ecovec_devices[] __initdata = {
	&heartbeat_device,
	&nor_flash_device,
};

static int __init devices_setup(void)
{
	/* enable SCIFA0 */
	gpio_request(GPIO_FN_SCIF0_TXD, NULL);
	gpio_request(GPIO_FN_SCIF0_RXD, NULL);
	gpio_request(GPIO_FN_SCIF0_SCK, NULL);

	/* enable debug LED */
	gpio_request(GPIO_PTG0, NULL);
	gpio_request(GPIO_PTG1, NULL);
	gpio_request(GPIO_PTG2, NULL);
	gpio_request(GPIO_PTG3, NULL);
	gpio_direction_output(GPIO_PTT0, 0);
	gpio_direction_output(GPIO_PTT1, 0);
	gpio_direction_output(GPIO_PTT2, 0);
	gpio_direction_output(GPIO_PTT3, 0);

	return platform_add_devices(ecovec_devices,
				    ARRAY_SIZE(ecovec_devices));
}
device_initcall(devices_setup);

static struct sh_machine_vector mv_ecovec __initmv = {
	.mv_name	= "R0P7724 (EcoVec)",
};
