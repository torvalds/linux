/*
 * $Id: walnut.c,v 1.3 2005/11/07 11:14:29 gleixner Exp $
 *
 * Mapping for Walnut flash
 * (used ebony.c as a "framework")
 *
 * Heikki Lindholm <holindho@infradead.org>
 *
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */

#include <linux/module.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/mtd/mtd.h>
#include <linux/mtd/map.h>
#include <linux/mtd/partitions.h>
#include <asm/io.h>
#include <asm/ibm4xx.h>
#include <platforms/4xx/walnut.h>

/* these should be in platforms/4xx/walnut.h ? */
#define WALNUT_FLASH_ONBD_N(x)		(x & 0x02)
#define WALNUT_FLASH_SRAM_SEL(x)	(x & 0x01)
#define WALNUT_FLASH_LOW		0xFFF00000
#define WALNUT_FLASH_HIGH		0xFFF80000
#define WALNUT_FLASH_SIZE		0x80000

static struct mtd_info *flash;

static struct map_info walnut_map = {
	.name =		"Walnut flash",
	.size =		WALNUT_FLASH_SIZE,
	.bankwidth =	1,
};

/* Actually, OpenBIOS is the last 128 KiB of the flash - better
 * partitioning could be made */
static struct mtd_partition walnut_partitions[] = {
	{
		.name =   "OpenBIOS",
		.offset = 0x0,
		.size =   WALNUT_FLASH_SIZE,
		/*.mask_flags = MTD_WRITEABLE, */ /* force read-only */
	}
};

int __init init_walnut(void)
{
	u8 fpga_brds1;
	void *fpga_brds1_adr;
	void *fpga_status_adr;
	unsigned long flash_base;

	/* this should already be mapped (platform/4xx/walnut.c) */
	fpga_status_adr = ioremap(WALNUT_FPGA_BASE, 8);
	if (!fpga_status_adr)
		return -ENOMEM;

	fpga_brds1_adr = fpga_status_adr+5;
	fpga_brds1 = readb(fpga_brds1_adr);
	/* iounmap(fpga_status_adr); */

	if (WALNUT_FLASH_ONBD_N(fpga_brds1)) {
		printk("The on-board flash is disabled (U79 sw 5)!");
		iounmap(fpga_status_adr);
		return -EIO;
	}
	if (WALNUT_FLASH_SRAM_SEL(fpga_brds1))
		flash_base = WALNUT_FLASH_LOW;
	else
		flash_base = WALNUT_FLASH_HIGH;

	walnut_map.phys = flash_base;
	walnut_map.virt =
		(void __iomem *)ioremap(flash_base, walnut_map.size);

	if (!walnut_map.virt) {
		printk("Failed to ioremap flash.\n");
		iounmap(fpga_status_adr);
		return -EIO;
	}

	simple_map_init(&walnut_map);

	flash = do_map_probe("jedec_probe", &walnut_map);
	if (flash) {
		flash->owner = THIS_MODULE;
		add_mtd_partitions(flash, walnut_partitions,
					ARRAY_SIZE(walnut_partitions));
	} else {
		printk("map probe failed for flash\n");
		iounmap(fpga_status_adr);
		return -ENXIO;
	}

	iounmap(fpga_status_adr);
	return 0;
}

static void __exit cleanup_walnut(void)
{
	if (flash) {
		del_mtd_partitions(flash);
		map_destroy(flash);
	}

	if (walnut_map.virt) {
		iounmap((void *)walnut_map.virt);
		walnut_map.virt = 0;
	}
}

module_init(init_walnut);
module_exit(cleanup_walnut);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Heikki Lindholm <holindho@infradead.org>");
MODULE_DESCRIPTION("MTD map and partitions for IBM 405GP Walnut boards");
