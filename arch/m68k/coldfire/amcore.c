/*
 * amcore.c -- Support for Sysam AMCORE open board
 *
 * (C) Copyright 2016, Angelo Dureghello <angelo@sysam.it>
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file COPYING in the main directory of this archive
 * for more details.
 */

#include <linux/device.h>
#include <linux/platform_device.h>
#include <linux/dm9000.h>
#include <linux/irq.h>
#include <linux/interrupt.h>
#include <linux/mtd/mtd.h>
#include <linux/mtd/map.h>
#include <linux/mtd/partitions.h>
#include <linux/mtd/physmap.h>
#include <linux/i2c.h>

#include <asm/coldfire.h>
#include <asm/mcfsim.h>
#include <asm/io.h>

#if IS_ENABLED(CONFIG_DM9000)

#define DM9000_IRQ	25
#define DM9000_ADDR	0x30000000

/*
 * DEVICES and related device RESOURCES
 */
static struct resource dm9000_resources[] = {
	/* physical address of the address register (CMD [A2] to 0)*/
	[0] = {
		.start  = DM9000_ADDR,
		.end    = DM9000_ADDR,
		.flags  = IORESOURCE_MEM,
	},
	/*
	 * physical address of the data register (CMD [A2] to 1),
	 * driver wants a range >=4 to assume a 32bit data bus
	 */
	[1] = {
		.start  = DM9000_ADDR + 4,
		.end    = DM9000_ADDR + 7,
		.flags  = IORESOURCE_MEM,
	},
	/* IRQ line the device's interrupt pin is connected to */
	[2] = {
		.start  = DM9000_IRQ,
		.end    = DM9000_IRQ,
		.flags  = IORESOURCE_IRQ,
	},
};

static struct dm9000_plat_data dm9000_platdata = {
	.flags		= DM9000_PLATF_32BITONLY,
};

static struct platform_device dm9000_device = {
	.name           = "dm9000",
	.id             = 0,
	.num_resources  = ARRAY_SIZE(dm9000_resources),
	.resource       = dm9000_resources,
	.dev = {
		.platform_data = &dm9000_platdata,
	}
};
#endif

static void __init dm9000_pre_init(void)
{
	/* Set the dm9000 interrupt to be auto-vectored */
	mcf_autovector(DM9000_IRQ);
}

/*
 * Partitioning of parallel NOR flash (39VF3201B)
 */
static struct mtd_partition amcore_partitions[] = {
	{
		.name	= "U-Boot (128K)",
		.size	= 0x20000,
		.offset	= 0x0
	},
	{
		.name	= "Kernel+ROMfs (2994K)",
		.size	= 0x2E0000,
		.offset	= MTDPART_OFS_APPEND
	},
	{
		.name	= "Flash Free Space (1024K)",
		.size	= MTDPART_SIZ_FULL,
		.offset	= MTDPART_OFS_APPEND
	}
};

static struct physmap_flash_data flash_data = {
	.parts		= amcore_partitions,
	.nr_parts	= ARRAY_SIZE(amcore_partitions),
	.width		= 2,
};

static struct resource flash_resource = {
	.start		= 0xffc00000,
	.end		= 0xffffffff,
	.flags		= IORESOURCE_MEM,
};

static struct platform_device flash_device = {
	.name		= "physmap-flash",
	.id		= -1,
	.resource	= &flash_resource,
	.num_resources	= 1,
	.dev		= {
		.platform_data	= &flash_data,
	},
};

static struct platform_device rtc_device = {
	.name	= "rtc-ds1307",
	.id	= -1,
};

static struct i2c_board_info amcore_i2c_info[] __initdata = {
	{
		I2C_BOARD_INFO("ds1338", 0x68),
	},
};

static struct platform_device *amcore_devices[] __initdata = {
#if IS_ENABLED(CONFIG_DM9000)
	&dm9000_device,
#endif
	&flash_device,
	&rtc_device,
};

static int __init init_amcore(void)
{
#if IS_ENABLED(CONFIG_DM9000)
	dm9000_pre_init();
#endif

	/* Add i2c RTC Dallas chip supprt */
	i2c_register_board_info(0, amcore_i2c_info,
				ARRAY_SIZE(amcore_i2c_info));

	platform_add_devices(amcore_devices, ARRAY_SIZE(amcore_devices));

	return 0;
}

arch_initcall(init_amcore);
