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
#include <asm/machvec.h>
#include <asm/io.h>

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


static struct platform_device *rsk7203_devices[] __initdata = {
	&smc911x_device,
	&flash_device,
};

static int __init rsk7203_devices_setup(void)
{
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
