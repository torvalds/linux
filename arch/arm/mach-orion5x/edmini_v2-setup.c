/*
 * arch/arm/mach-orion5x/edmini_v2-setup.c
 *
 * LaCie Ethernet Disk mini V2 Setup
 *
 * Copyright (C) 2008 Christopher Moore <moore@free.fr>
 * Copyright (C) 2008 Albert Aribaud <albert.aribaud@free.fr>
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2. This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */

/*
 * TODO: add Orion USB device port init when kernel.org support is added.
 * TODO: add flash write support: see below.
 * TODO: add power-off support.
 * TODO: add I2C EEPROM support.
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/pci.h>
#include <linux/irq.h>
#include <linux/mbus.h>
#include <linux/mtd/physmap.h>
#include <linux/leds.h>
#include <linux/gpio_keys.h>
#include <linux/input.h>
#include <linux/i2c.h>
#include <linux/ata_platform.h>
#include <linux/gpio.h>
#include <asm/mach-types.h>
#include <asm/mach/arch.h>
#include <asm/mach/pci.h>
#include <mach/orion5x.h>
#include "common.h"
#include "mpp.h"

/*****************************************************************************
 * EDMINI_V2 Info
 ****************************************************************************/

/*
 * 512KB NOR flash Device bus boot chip select
 */

#define EDMINI_V2_NOR_BOOT_BASE		0xfff80000
#define EDMINI_V2_NOR_BOOT_SIZE		SZ_512K

/*****************************************************************************
 * 512KB NOR Flash on BOOT Device
 ****************************************************************************/

/*
 * Currently the MTD code does not recognize the MX29LV400CBCT as a bottom
 * -type device. This could cause risks of accidentally erasing critical
 * flash sectors. We thus define a single, write-protected partition covering
 * the whole flash.
 * TODO: once the flash part TOP/BOTTOM detection issue is sorted out in the MTD
 * code, break this into at least three partitions: 'u-boot code', 'u-boot
 * environment' and 'whatever is left'.
 */

static struct mtd_partition edmini_v2_partitions[] = {
	{
		.name		= "Full512kb",
		.size		= 0x00080000,
		.offset		= 0x00000000,
		.mask_flags	= MTD_WRITEABLE,
	},
};

static struct physmap_flash_data edmini_v2_nor_flash_data = {
	.width		= 1,
	.parts		= edmini_v2_partitions,
	.nr_parts	= ARRAY_SIZE(edmini_v2_partitions),
};

static struct resource edmini_v2_nor_flash_resource = {
	.flags			= IORESOURCE_MEM,
	.start			= EDMINI_V2_NOR_BOOT_BASE,
	.end			= EDMINI_V2_NOR_BOOT_BASE
		+ EDMINI_V2_NOR_BOOT_SIZE - 1,
};

static struct platform_device edmini_v2_nor_flash = {
	.name			= "physmap-flash",
	.id			= 0,
	.dev		= {
		.platform_data	= &edmini_v2_nor_flash_data,
	},
	.num_resources		= 1,
	.resource		= &edmini_v2_nor_flash_resource,
};

/*****************************************************************************
 * General Setup
 ****************************************************************************/

void __init edmini_v2_init(void)
{
	/*
	 * Configure peripherals.
	 */
	orion5x_ehci0_init();

	mvebu_mbus_add_window_by_id(ORION_MBUS_DEVBUS_BOOT_TARGET,
				    ORION_MBUS_DEVBUS_BOOT_ATTR,
				    EDMINI_V2_NOR_BOOT_BASE,
				    EDMINI_V2_NOR_BOOT_SIZE);
	platform_device_register(&edmini_v2_nor_flash);

	pr_notice("edmini_v2: USB device port, flash write and power-off "
		  "are not yet supported.\n");
}
