// SPDX-License-Identifier: GPL-2.0-only
/*
 * arch/arm/mach-ks8695/board-acs5k.c
 *
 * Brivo Systems LLC, ACS-5000 Master Board
 *
 * Copyright 2008 Simtec Electronics
 *		  Daniel Silverstone <dsilvers@simtec.co.uk>
 */
#include <linux/gpio.h>
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/interrupt.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/gpio/machine.h>
#include <linux/i2c.h>
#include <linux/i2c-algo-bit.h>
#include <linux/platform_data/i2c-gpio.h>
#include <linux/platform_data/pca953x.h>

#include <linux/mtd/mtd.h>
#include <linux/mtd/map.h>
#include <linux/mtd/physmap.h>
#include <linux/mtd/partitions.h>

#include <asm/mach-types.h>

#include <asm/mach/arch.h>
#include <asm/mach/map.h>
#include <asm/mach/irq.h>

#include "devices.h"
#include <mach/gpio-ks8695.h>

#include "generic.h"

static struct gpiod_lookup_table acs5k_i2c_gpiod_table = {
	.dev_id		= "i2c-gpio",
	.table		= {
		GPIO_LOOKUP_IDX("KS8695", 4, NULL, 0,
				GPIO_ACTIVE_HIGH | GPIO_OPEN_DRAIN),
		GPIO_LOOKUP_IDX("KS8695", 5, NULL, 1,
				GPIO_ACTIVE_HIGH | GPIO_OPEN_DRAIN),
	},
};

static struct i2c_gpio_platform_data acs5k_i2c_device_platdata = {
	.udelay		= 10,
};

static struct platform_device acs5k_i2c_device = {
	.name		= "i2c-gpio",
	.id		= -1,
	.num_resources	= 0,
	.resource	= NULL,
	.dev		= {
		.platform_data	= &acs5k_i2c_device_platdata,
	},
};

static int acs5k_pca9555_setup(struct i2c_client *client,
			       unsigned gpio_base, unsigned ngpio,
			       void *context)
{
	static int acs5k_gpio_value[] = {
		-1, -1, -1, -1, -1, -1, -1, 0, 1, 1, -1, 0, 1, 0, -1, -1
	};
	int n;

	for (n = 0; n < ARRAY_SIZE(acs5k_gpio_value); ++n) {
		gpio_request(gpio_base + n, "ACS-5000 GPIO Expander");
		if (acs5k_gpio_value[n] < 0)
			gpio_direction_input(gpio_base + n);
		else
			gpio_direction_output(gpio_base + n,
					      acs5k_gpio_value[n]);
		gpio_export(gpio_base + n, 0); /* Export, direction locked down */
	}

	return 0;
}

static struct pca953x_platform_data acs5k_i2c_pca9555_platdata = {
	.gpio_base	= 16, /* Start directly after the CPU's GPIO */
	.invert		= 0, /* Do not invert */
	.setup		= acs5k_pca9555_setup,
};

static struct i2c_board_info acs5k_i2c_devs[] __initdata = {
	{
		I2C_BOARD_INFO("pcf8563", 0x51),
	},
	{
		I2C_BOARD_INFO("pca9555", 0x20),
		.platform_data = &acs5k_i2c_pca9555_platdata,
	},
};

static void __init acs5k_i2c_init(void)
{
	/* The gpio interface */
	gpiod_add_lookup_table(&acs5k_i2c_gpiod_table);
	platform_device_register(&acs5k_i2c_device);
	/* I2C devices */
	i2c_register_board_info(0, acs5k_i2c_devs,
				ARRAY_SIZE(acs5k_i2c_devs));
}

static struct mtd_partition acs5k_nor_partitions[] = {
	[0] = {
		.name	= "Boot Agent and config",
		.size	= SZ_256K,
		.offset	= 0,
		.mask_flags = MTD_WRITEABLE,
	},
	[1] = {
		.name	= "Kernel",
		.size	= SZ_1M,
		.offset	= SZ_256K,
	},
	[2] = {
		.name	= "SquashFS1",
		.size	= SZ_2M,
		.offset	= SZ_256K + SZ_1M,
	},
	[3] = {
		.name	= "SquashFS2",
		.size	= SZ_4M + SZ_2M,
		.offset	= SZ_256K + SZ_1M + SZ_2M,
	},
	[4] = {
		.name	= "Data",
		.size	= SZ_16M + SZ_4M + SZ_2M + SZ_512K, /* 22.5 MB */
		.offset	= SZ_256K + SZ_8M + SZ_1M,
	}
};

static struct physmap_flash_data acs5k_nor_pdata = {
	.width		= 4,
	.nr_parts	= ARRAY_SIZE(acs5k_nor_partitions),
	.parts		= acs5k_nor_partitions,
};

static struct resource acs5k_nor_resource[] = {
	[0] = {
		.start = SZ_32M, /* We expect the bootloader to map
				  * the flash here.
				  */
		.end   = SZ_32M + SZ_16M - 1,
		.flags = IORESOURCE_MEM,
	},
	[1] = {
		.start = SZ_32M + SZ_16M,
		.end   = SZ_32M + SZ_32M - SZ_256K - 1,
		.flags = IORESOURCE_MEM,
	}
};

static struct platform_device acs5k_device_nor = {
	.name		= "physmap-flash",
	.id		= -1,
	.num_resources	= ARRAY_SIZE(acs5k_nor_resource),
	.resource	= acs5k_nor_resource,
	.dev		= {
		.platform_data = &acs5k_nor_pdata,
	},
};

static void __init acs5k_register_nor(void)
{
	int ret;

	if (acs5k_nor_partitions[0].mask_flags == 0)
		printk(KERN_WARNING "Warning: Unprotecting bootloader and configuration partition\n");

	ret = platform_device_register(&acs5k_device_nor);
	if (ret < 0)
		printk(KERN_ERR "failed to register physmap-flash device\n");
}

static int __init acs5k_protection_setup(char *s)
{
	/* We can't allocate anything here but we should be able
	 * to trivially parse s and decide if we can protect the
	 * bootloader partition or not
	 */
	if (strcmp(s, "no") == 0)
		acs5k_nor_partitions[0].mask_flags = 0;

	return 1;
}

__setup("protect_bootloader=", acs5k_protection_setup);

static void __init acs5k_init_gpio(void)
{
	int i;

	ks8695_register_gpios();
	for (i = 0; i < 4; ++i)
		gpio_request(i, "ACS5K IRQ");
	gpio_request(7, "ACS5K KS_FRDY");
	for (i = 8; i < 16; ++i)
		gpio_request(i, "ACS5K Unused");

	gpio_request(3, "ACS5K CAN Control");
	gpio_request(6, "ACS5K Heartbeat");
	gpio_direction_output(3, 1); /* Default CAN_RESET high */
	gpio_direction_output(6, 0); /* Default KS8695_ACTIVE low */
	gpio_export(3, 0); /* export CAN_RESET as output only */
	gpio_export(6, 0); /* export KS8695_ACTIVE as output only */
}

static void __init acs5k_init(void)
{
	acs5k_init_gpio();

	/* Network device */
	ks8695_add_device_lan();	/* eth0 = LAN */
	ks8695_add_device_wan();	/* ethX = WAN */

	/* NOR devices */
	acs5k_register_nor();

	/* I2C bus */
	acs5k_i2c_init();
}

MACHINE_START(ACS5K, "Brivo Systems LLC ACS-5000 Master board")
	/* Maintainer: Simtec Electronics. */
	.atag_offset	= 0x100,
	.map_io		= ks8695_map_io,
	.init_irq	= ks8695_init_irq,
	.init_machine	= acs5k_init,
	.init_time	= ks8695_timer_init,
	.restart	= ks8695_restart,
MACHINE_END
