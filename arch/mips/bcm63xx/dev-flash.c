/*
 * Broadcom BCM63xx flash registration
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2008 Maxime Bizon <mbizon@freebox.fr>
 * Copyright (C) 2008 Florian Fainelli <florian@openwrt.org>
 */

#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/platform_device.h>
#include <linux/mtd/mtd.h>
#include <linux/mtd/partitions.h>
#include <linux/mtd/physmap.h>

#include <bcm63xx_cpu.h>
#include <bcm63xx_dev_flash.h>
#include <bcm63xx_regs.h>
#include <bcm63xx_io.h>

static struct mtd_partition mtd_partitions[] = {
	{
		.name		= "cfe",
		.offset		= 0x0,
		.size		= 0x40000,
	}
};

static const char *bcm63xx_part_types[] = { "bcm63xxpart", NULL };

static struct physmap_flash_data flash_data = {
	.width			= 2,
	.parts			= mtd_partitions,
	.part_probe_types	= bcm63xx_part_types,
};

static struct resource mtd_resources[] = {
	{
		.start		= 0,	/* filled at runtime */
		.end		= 0,	/* filled at runtime */
		.flags		= IORESOURCE_MEM,
	}
};

static struct platform_device mtd_dev = {
	.name			= "physmap-flash",
	.resource		= mtd_resources,
	.num_resources		= ARRAY_SIZE(mtd_resources),
	.dev			= {
		.platform_data	= &flash_data,
	},
};

int __init bcm63xx_flash_register(void)
{
	u32 val;

	/* read base address of boot chip select (0) */
	val = bcm_mpi_readl(MPI_CSBASE_REG(0));
	val &= MPI_CSBASE_BASE_MASK;

	mtd_resources[0].start = val;
	mtd_resources[0].end = 0x1FFFFFFF;

	return platform_device_register(&mtd_dev);
}
