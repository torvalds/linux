/*
 *  linux/arch/arm/mach-clps711x/cdb89712.c
 *
 *  Copyright (C) 2000-2001 Deep Blue Solutions Ltd
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/types.h>
#include <linux/string.h>
#include <linux/mm.h>
#include <linux/io.h>
#include <linux/interrupt.h>
#include <linux/platform_device.h>

#include <linux/mtd/physmap.h>
#include <linux/mtd/plat-ram.h>
#include <linux/mtd/partitions.h>

#include <mach/hardware.h>
#include <asm/pgtable.h>
#include <asm/page.h>
#include <asm/setup.h>
#include <asm/mach-types.h>
#include <asm/mach/arch.h>
#include <asm/mach/map.h>

#include "common.h"
#include "devices.h"

#define CDB89712_CS8900_BASE	(CS2_PHYS_BASE + 0x300)
#define CDB89712_CS8900_IRQ	(IRQ_EINT3)

static struct resource cdb89712_cs8900_resource[] __initdata = {
	DEFINE_RES_MEM(CDB89712_CS8900_BASE, SZ_1K),
	DEFINE_RES_IRQ(CDB89712_CS8900_IRQ),
};

static struct mtd_partition cdb89712_flash_partitions[] __initdata = {
	{
		.name	= "Flash",
		.offset	= 0,
		.size	= MTDPART_SIZ_FULL,
	},
};

static struct physmap_flash_data cdb89712_flash_pdata __initdata = {
	.width		= 4,
	.probe_type	= "map_rom",
	.parts		= cdb89712_flash_partitions,
	.nr_parts	= ARRAY_SIZE(cdb89712_flash_partitions),
};

static struct resource cdb89712_flash_resources[] __initdata = {
	DEFINE_RES_MEM(CS0_PHYS_BASE, SZ_8M),
};

static struct platform_device cdb89712_flash_pdev __initdata = {
	.name		= "physmap-flash",
	.id		= 0,
	.resource	= cdb89712_flash_resources,
	.num_resources	= ARRAY_SIZE(cdb89712_flash_resources),
	.dev	= {
		.platform_data	= &cdb89712_flash_pdata,
	},
};

static struct mtd_partition cdb89712_bootrom_partitions[] __initdata = {
	{
		.name	= "BootROM",
		.offset	= 0,
		.size	= MTDPART_SIZ_FULL,
	},
};

static struct physmap_flash_data cdb89712_bootrom_pdata __initdata = {
	.width		= 4,
	.probe_type	= "map_rom",
	.parts		= cdb89712_bootrom_partitions,
	.nr_parts	= ARRAY_SIZE(cdb89712_bootrom_partitions),
};

static struct resource cdb89712_bootrom_resources[] __initdata = {
	DEFINE_RES_NAMED(CS7_PHYS_BASE, SZ_128, "BOOTROM", IORESOURCE_MEM |
			 IORESOURCE_READONLY),
};

static struct platform_device cdb89712_bootrom_pdev __initdata = {
	.name		= "physmap-flash",
	.id		= 1,
	.resource	= cdb89712_bootrom_resources,
	.num_resources	= ARRAY_SIZE(cdb89712_bootrom_resources),
	.dev	= {
		.platform_data	= &cdb89712_bootrom_pdata,
	},
};

static struct platdata_mtd_ram cdb89712_sram_pdata __initdata = {
	.bankwidth	= 4,
};

static struct resource cdb89712_sram_resources[] __initdata = {
	DEFINE_RES_MEM(CLPS711X_SRAM_BASE, CLPS711X_SRAM_SIZE),
};

static struct platform_device cdb89712_sram_pdev __initdata = {
	.name		= "mtd-ram",
	.id		= 0,
	.resource	= cdb89712_sram_resources,
	.num_resources	= ARRAY_SIZE(cdb89712_sram_resources),
	.dev	= {
		.platform_data	= &cdb89712_sram_pdata,
	},
};

static void __init cdb89712_init(void)
{
	clps711x_devices_init();
	platform_device_register(&cdb89712_flash_pdev);
	platform_device_register(&cdb89712_bootrom_pdev);
	platform_device_register(&cdb89712_sram_pdev);
	platform_device_register_simple("cs89x0", 0, cdb89712_cs8900_resource,
					ARRAY_SIZE(cdb89712_cs8900_resource));
}

MACHINE_START(CDB89712, "Cirrus-CDB89712")
	/* Maintainer: Ray Lehtiniemi */
	.atag_offset	= 0x100,
	.map_io		= clps711x_map_io,
	.init_irq	= clps711x_init_irq,
	.init_time	= clps711x_timer_init,
	.init_machine	= cdb89712_init,
	.restart	= clps711x_restart,
MACHINE_END
