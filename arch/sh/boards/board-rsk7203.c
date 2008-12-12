/*
 * Renesas Technology Europe RSK+ 7203 Support.
 *
 * Copyright (C) 2008 Paul Mundt
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 */
#include <linux/init.h>
#include <linux/types.h>
#include <linux/platform_device.h>
#include <linux/interrupt.h>
#include <linux/mtd/mtd.h>
#include <linux/mtd/partitions.h>
#include <linux/mtd/physmap.h>
#include <linux/mtd/map.h>
#include <linux/smc911x.h>
#include <linux/gpio.h>
#include <linux/leds.h>
#include <asm/machvec.h>
#include <asm/io.h>
#include <cpu/sh7203.h>

static struct smc911x_platdata smc911x_info = {
	.flags		= SMC911X_USE_16BIT,
	.irq_flags	= IRQF_TRIGGER_LOW,
};

static struct resource smc911x_resources[] = {
	[0] = {
		.start		= 0x24000000,
		.end		= 0x24000000 + 0x100,
		.flags		= IORESOURCE_MEM,
	},
	[1] = {
		.start		= 64,
		.end		= 64,
		.flags		= IORESOURCE_IRQ,
	},
};

static struct platform_device smc911x_device = {
	.name		= "smc911x",
	.id		= -1,
	.num_resources	= ARRAY_SIZE(smc911x_resources),
	.resource	= smc911x_resources,
	.dev		= {
		.platform_data = &smc911x_info,
	},
};

static const char *probes[] = { "cmdlinepart", NULL };

static struct mtd_partition *parsed_partitions;

static struct mtd_partition rsk7203_partitions[] = {
	{
		.name		= "Bootloader",
		.offset		= 0x00000000,
		.size		= 0x00040000,
		.mask_flags	= MTD_WRITEABLE,
	}, {
		.name		= "Kernel",
		.offset		= MTDPART_OFS_NXTBLK,
		.size		= 0x001c0000,
	}, {
		.name		= "Flash_FS",
		.offset		= MTDPART_OFS_NXTBLK,
		.size		= MTDPART_SIZ_FULL,
	}
};

static struct physmap_flash_data flash_data = {
	.width		= 2,
};

static struct resource flash_resource = {
	.start		= 0x20000000,
	.end		= 0x20400000,
	.flags		= IORESOURCE_MEM,
};

static struct platform_device flash_device = {
	.name		= "physmap-flash",
	.id		= -1,
	.resource	= &flash_resource,
	.num_resources	= 1,
	.dev		= {
		.platform_data = &flash_data,
	},
};

static struct mtd_info *flash_mtd;

static struct map_info rsk7203_flash_map = {
	.name		= "RSK+ Flash",
	.size		= 0x400000,
	.bankwidth	= 2,
};

static void __init set_mtd_partitions(void)
{
	int nr_parts = 0;

	simple_map_init(&rsk7203_flash_map);
	flash_mtd = do_map_probe("cfi_probe", &rsk7203_flash_map);
	nr_parts = parse_mtd_partitions(flash_mtd, probes,
					&parsed_partitions, 0);
	/* If there is no partition table, used the hard coded table */
	if (nr_parts <= 0) {
		flash_data.parts = rsk7203_partitions;
		flash_data.nr_parts = ARRAY_SIZE(rsk7203_partitions);
	} else {
		flash_data.nr_parts = nr_parts;
		flash_data.parts = parsed_partitions;
	}
}

static struct gpio_led rsk7203_gpio_leds[] = {
	{
		.name			= "green",
		.gpio			= GPIO_PE10,
		.active_low		= 1,
	}, {
		.name			= "orange",
		.default_trigger	= "nand-disk",
		.gpio			= GPIO_PE12,
		.active_low		= 1,
	}, {
		.name			= "red:timer",
		.default_trigger	= "timer",
		.gpio			= GPIO_PC14,
		.active_low		= 1,
	}, {
		.name			= "red:heartbeat",
		.default_trigger	= "heartbeat",
		.gpio			= GPIO_PE11,
		.active_low		= 1,
	},
};

static struct gpio_led_platform_data rsk7203_gpio_leds_info = {
	.leds		= rsk7203_gpio_leds,
	.num_leds	= ARRAY_SIZE(rsk7203_gpio_leds),
};

static struct platform_device led_device = {
	.name		= "leds-gpio",
	.id		= -1,
	.dev		= {
		.platform_data	= &rsk7203_gpio_leds_info,
	},
};

static struct platform_device *rsk7203_devices[] __initdata = {
	&smc911x_device,
	&flash_device,
	&led_device,
};

static int __init rsk7203_devices_setup(void)
{
	/* Select pins for SCIF0 */
	gpio_request(GPIO_FN_TXD0, NULL);
	gpio_request(GPIO_FN_RXD0, NULL);

	set_mtd_partitions();
	return platform_add_devices(rsk7203_devices,
				    ARRAY_SIZE(rsk7203_devices));
}
device_initcall(rsk7203_devices_setup);

/*
 * The Machine Vector
 */
static struct sh_machine_vector mv_rsk7203 __initmv = {
	.mv_name        = "RSK+7203",
};
