/*
 * Broadcom BCM63xx flash registration
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2008 Maxime Bizon <mbizon@freebox.fr>
 * Copyright (C) 2008 Florian Fainelli <florian@openwrt.org>
 * Copyright (C) 2012 Jonas Gorski <jonas.gorski@gmail.com>
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

static int __init bcm63xx_detect_flash_type(void)
{
	u32 val;

	switch (bcm63xx_get_cpu_id()) {
	case BCM6328_CPU_ID:
		val = bcm_misc_readl(MISC_STRAPBUS_6328_REG);
		if (val & STRAPBUS_6328_BOOT_SEL_SERIAL)
			return BCM63XX_FLASH_TYPE_SERIAL;
		else
			return BCM63XX_FLASH_TYPE_NAND;
	case BCM6338_CPU_ID:
	case BCM6345_CPU_ID:
	case BCM6348_CPU_ID:
		/* no way to auto detect so assume parallel */
		return BCM63XX_FLASH_TYPE_PARALLEL;
	case BCM6358_CPU_ID:
		val = bcm_gpio_readl(GPIO_STRAPBUS_REG);
		if (val & STRAPBUS_6358_BOOT_SEL_PARALLEL)
			return BCM63XX_FLASH_TYPE_PARALLEL;
		else
			return BCM63XX_FLASH_TYPE_SERIAL;
	case BCM6368_CPU_ID:
		val = bcm_gpio_readl(GPIO_STRAPBUS_REG);
		switch (val & STRAPBUS_6368_BOOT_SEL_MASK) {
		case STRAPBUS_6368_BOOT_SEL_NAND:
			return BCM63XX_FLASH_TYPE_NAND;
		case STRAPBUS_6368_BOOT_SEL_SERIAL:
			return BCM63XX_FLASH_TYPE_SERIAL;
		case STRAPBUS_6368_BOOT_SEL_PARALLEL:
			return BCM63XX_FLASH_TYPE_PARALLEL;
		}
	default:
		return -EINVAL;
	}
}

int __init bcm63xx_flash_register(void)
{
	int flash_type;
	u32 val;

	flash_type = bcm63xx_detect_flash_type();

	switch (flash_type) {
	case BCM63XX_FLASH_TYPE_PARALLEL:
		/* read base address of boot chip select (0) */
		val = bcm_mpi_readl(MPI_CSBASE_REG(0));
		val &= MPI_CSBASE_BASE_MASK;

		mtd_resources[0].start = val;
		mtd_resources[0].end = 0x1FFFFFFF;

		return platform_device_register(&mtd_dev);
	case BCM63XX_FLASH_TYPE_SERIAL:
		pr_warn("unsupported serial flash detected\n");
		return -ENODEV;
	case BCM63XX_FLASH_TYPE_NAND:
		pr_warn("unsupported NAND flash detected\n");
		return -ENODEV;
	default:
		pr_err("flash detection failed for BCM%x: %d\n",
		       bcm63xx_get_cpu_id(), flash_type);
		return -ENODEV;
	}
}
