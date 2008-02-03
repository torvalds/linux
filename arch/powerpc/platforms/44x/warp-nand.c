/*
 * PIKA Warp(tm) NAND flash specific routines
 *
 * Copyright (c) 2008 PIKA Technologies
 *   Sean MacLennan <smaclennan@pikatech.com>
 */

#include <linux/platform_device.h>
#include <linux/mtd/mtd.h>
#include <linux/mtd/map.h>
#include <linux/mtd/partitions.h>
#include <linux/mtd/nand.h>
#include <linux/mtd/ndfc.h>

#ifdef CONFIG_MTD_NAND_NDFC

#define CS_NAND_0	1	/* use chip select 1 for NAND device 0 */

#define WARP_NAND_FLASH_REG_ADDR	0xD0000000UL
#define WARP_NAND_FLASH_REG_SIZE	0x2000

static struct resource warp_ndfc = {
	.start = WARP_NAND_FLASH_REG_ADDR,
	.end   = WARP_NAND_FLASH_REG_ADDR + WARP_NAND_FLASH_REG_SIZE,
	.flags = IORESOURCE_MEM,
};

static struct mtd_partition nand_parts[] = {
	{
		.name   = "kernel",
		.offset = 0,
		.size   = 0x0200000
	},
	{
		.name   = "root",
		.offset = 0x0200000,
		.size   = 0x3400000
	},
	{
		.name   = "user",
		.offset = 0x3600000,
		.size   = 0x0A00000
	},
};

struct ndfc_controller_settings warp_ndfc_settings = {
	.ccr_settings = (NDFC_CCR_BS(CS_NAND_0) | NDFC_CCR_ARAC1),
	.ndfc_erpn = 0,
};

static struct ndfc_chip_settings warp_chip0_settings = {
	.bank_settings = 0x80002222,
};

struct platform_nand_ctrl warp_nand_ctrl = {
	.priv = &warp_ndfc_settings,
};

static struct platform_device warp_ndfc_device = {
	.name = "ndfc-nand",
	.id = 0,
	.dev = {
		.platform_data = &warp_nand_ctrl,
	},
	.num_resources = 1,
	.resource = &warp_ndfc,
};

static struct nand_ecclayout nand_oob_16 = {
	.eccbytes = 3,
	.eccpos = { 0, 1, 2, 3, 6, 7 },
	.oobfree = { {.offset = 8, .length = 16} }
};

static struct platform_nand_chip warp_nand_chip0 = {
	.nr_chips = 1,
	.chip_offset = CS_NAND_0,
	.nr_partitions = ARRAY_SIZE(nand_parts),
	.partitions = nand_parts,
	.chip_delay = 50,
	.ecclayout = &nand_oob_16,
	.priv = &warp_chip0_settings,
};

static struct platform_device warp_nand_device = {
	.name = "ndfc-chip",
	.id = 0,
	.num_resources = 1,
	.resource = &warp_ndfc,
	.dev = {
		.platform_data = &warp_nand_chip0,
		.parent = &warp_ndfc_device.dev,
	}
};

static int warp_setup_nand_flash(void)
{
	platform_device_register(&warp_ndfc_device);
	platform_device_register(&warp_nand_device);

	return 0;
}
device_initcall(warp_setup_nand_flash);

#endif
