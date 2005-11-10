/*
 * Mapping for Ocotea user flash
 *
 * Matt Porter <mporter@kernel.crashing.org>
 *
 * Copyright 2002-2004 MontaVista Software Inc.
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
#include <linux/config.h>
#include <asm/io.h>
#include <asm/ibm44x.h>
#include <platforms/4xx/ocotea.h>

static struct mtd_info *flash;

static struct map_info ocotea_small_map = {
	.name =		"Ocotea small flash",
	.size =		OCOTEA_SMALL_FLASH_SIZE,
	.buswidth =	1,
};

static struct map_info ocotea_large_map = {
	.name =		"Ocotea large flash",
	.size =		OCOTEA_LARGE_FLASH_SIZE,
	.buswidth =	1,
};

static struct mtd_partition ocotea_small_partitions[] = {
	{
		.name =   "pibs",
		.offset = 0x0,
		.size =   0x100000,
	}
};

static struct mtd_partition ocotea_large_partitions[] = {
	{
		.name =   "fs",
		.offset = 0,
		.size =   0x300000,
	},
	{
		.name =   "firmware",
		.offset = 0x300000,
		.size =   0x100000,
	}
};

#define NB_OF(x)  (sizeof(x)/sizeof(x[0]))

int __init init_ocotea(void)
{
	u8 fpga0_reg;
	u8 *fpga0_adr;
	unsigned long long small_flash_base, large_flash_base;

	fpga0_adr = ioremap64(OCOTEA_FPGA_ADDR, 16);
	if (!fpga0_adr)
		return -ENOMEM;

	fpga0_reg = readb((unsigned long)fpga0_adr);
	iounmap(fpga0_adr);

	if (OCOTEA_BOOT_LARGE_FLASH(fpga0_reg)) {
		small_flash_base = OCOTEA_SMALL_FLASH_HIGH;
		large_flash_base = OCOTEA_LARGE_FLASH_LOW;
	}
	else {
		small_flash_base = OCOTEA_SMALL_FLASH_LOW;
		large_flash_base = OCOTEA_LARGE_FLASH_HIGH;
	}

	ocotea_small_map.phys = small_flash_base;
	ocotea_small_map.virt = ioremap64(small_flash_base,
					 ocotea_small_map.size);

	if (!ocotea_small_map.virt) {
		printk("Failed to ioremap flash\n");
		return -EIO;
	}

	simple_map_init(&ocotea_small_map);

	flash = do_map_probe("map_rom", &ocotea_small_map);
	if (flash) {
		flash->owner = THIS_MODULE;
		add_mtd_partitions(flash, ocotea_small_partitions,
					NB_OF(ocotea_small_partitions));
	} else {
		printk("map probe failed for flash\n");
		return -ENXIO;
	}

	ocotea_large_map.phys = large_flash_base;
	ocotea_large_map.virt = ioremap64(large_flash_base,
					 ocotea_large_map.size);

	if (!ocotea_large_map.virt) {
		printk("Failed to ioremap flash\n");
		return -EIO;
	}

	simple_map_init(&ocotea_large_map);

	flash = do_map_probe("cfi_probe", &ocotea_large_map);
	if (flash) {
		flash->owner = THIS_MODULE;
		add_mtd_partitions(flash, ocotea_large_partitions,
					NB_OF(ocotea_large_partitions));
	} else {
		printk("map probe failed for flash\n");
		return -ENXIO;
	}

	return 0;
}

static void __exit cleanup_ocotea(void)
{
	if (flash) {
		del_mtd_partitions(flash);
		map_destroy(flash);
	}

	if (ocotea_small_map.virt) {
		iounmap((void *)ocotea_small_map.virt);
		ocotea_small_map.virt = 0;
	}

	if (ocotea_large_map.virt) {
		iounmap((void *)ocotea_large_map.virt);
		ocotea_large_map.virt = 0;
	}
}

module_init(init_ocotea);
module_exit(cleanup_ocotea);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Matt Porter <mporter@kernel.crashing.org>");
MODULE_DESCRIPTION("MTD map and partitions for IBM 440GX Ocotea boards");
