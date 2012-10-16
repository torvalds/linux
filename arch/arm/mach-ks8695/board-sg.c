/*
 * board-sg.c -- support for the SnapGear KS8695 based boards
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/mtd/mtd.h>
#include <linux/mtd/map.h>
#include <linux/mtd/physmap.h>
#include <linux/mtd/partitions.h>
#include <asm/mach-types.h>
#include <asm/mach/arch.h>
#include <mach/devices.h>
#include "generic.h"

/*
 * The SG310 machine type is fitted with a conventional 8MB Strataflash
 * device. Define its partitioning.
 */
#define	FL_BASE		0x02000000
#define	FL_SIZE		SZ_8M

static struct mtd_partition sg_mtd_partitions[] = {
	[0] = {
		.name	= "SnapGear Boot Loader",
		.size	= SZ_128K,
	},
	[1] = {
		.name	= "SnapGear non-volatile configuration",
		.size	= SZ_512K,
		.offset	= SZ_256K,
	},
	[2] = {
		.name	= "SnapGear image",
		.offset	= SZ_512K + SZ_256K,
	},
	[3] = {
		.name	= "SnapGear StrataFlash",
	},
	[4] = {
		.name	= "SnapGear Boot Tags",
		.size	= SZ_128K,
		.offset	= SZ_128K,
	},
};

static struct physmap_flash_data sg_mtd_pdata = {
	.width		= 1,
	.nr_parts	= ARRAY_SIZE(sg_mtd_partitions),
	.parts		= sg_mtd_partitions,
};


static struct resource sg_mtd_resource[] = {
	[0] = {
		.start = FL_BASE,
		.end   = FL_BASE + FL_SIZE - 1,
		.flags = IORESOURCE_MEM,
	},
};

static struct platform_device sg_mtd_device = {
	.name		= "physmap-flash",
	.id		= 0,
	.num_resources	= ARRAY_SIZE(sg_mtd_resource),
	.resource	= sg_mtd_resource,
	.dev		= {
		.platform_data = &sg_mtd_pdata,
	},
};

static void __init sg_init(void)
{
	ks8695_add_device_lan();
	ks8695_add_device_wan();

	if (machine_is_sg310())
		platform_device_register(&sg_mtd_device);
}

#ifdef CONFIG_MACH_LITE300
MACHINE_START(LITE300, "SecureComputing/SG300")
	/* SnapGear */
	.atag_offset	= 0x100,
	.map_io		= ks8695_map_io,
	.init_irq	= ks8695_init_irq,
	.init_machine	= sg_init,
	.timer		= &ks8695_timer,
	.restart	= ks8695_restart,
MACHINE_END
#endif

#ifdef CONFIG_MACH_SG310
MACHINE_START(SG310, "McAfee/SG310")
	/* SnapGear */
	.atag_offset	= 0x100,
	.map_io		= ks8695_map_io,
	.init_irq	= ks8695_init_irq,
	.init_machine	= sg_init,
	.timer		= &ks8695_timer,
	.restart	= ks8695_restart,
MACHINE_END
#endif

#ifdef CONFIG_MACH_SE4200
MACHINE_START(SE4200, "SecureComputing/SE4200")
	/* SnapGear */
	.atag_offset	= 0x100,
	.map_io		= ks8695_map_io,
	.init_irq	= ks8695_init_irq,
	.init_machine	= sg_init,
	.timer		= &ks8695_timer,
	.restart	= ks8695_restart,
MACHINE_END
#endif
