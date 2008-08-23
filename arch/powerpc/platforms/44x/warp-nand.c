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
#include <linux/of.h>
#include <asm/machdep.h>


#ifdef CONFIG_MTD_NAND_NDFC

#define CS_NAND_0	1	/* use chip select 1 for NAND device 0 */

#define WARP_NAND_FLASH_REG_ADDR	0xD0000000UL
#define WARP_NAND_FLASH_REG_SIZE	0x2000

static struct resource warp_ndfc = {
	.start = WARP_NAND_FLASH_REG_ADDR,
	.end   = WARP_NAND_FLASH_REG_ADDR + WARP_NAND_FLASH_REG_SIZE - 1,
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
		.size   = 0x3E00000
	},
	{
		.name   = "persistent",
		.offset = 0x4000000,
		.size   = 0x4000000
	},
	{
		.name   = "persistent1",
		.offset = 0x8000000,
		.size   = 0x4000000
	},
	{
		.name   = "persistent2",
		.offset = 0xC000000,
		.size   = 0x4000000
	}
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

/* Do NOT set the ecclayout: let it default so it is correct for both
 * 64M and 256M flash chips.
 */
static struct platform_nand_chip warp_nand_chip0 = {
	.nr_chips = 1,
	.chip_offset = CS_NAND_0,
	.nr_partitions = ARRAY_SIZE(nand_parts),
	.partitions = nand_parts,
	.chip_delay = 20,
	.priv = &warp_chip0_settings,
};

static struct platform_device warp_nand_device = {
	.name = "ndfc-chip",
	.id = 0,
	.num_resources = 0,
	.dev = {
		.platform_data = &warp_nand_chip0,
		.parent = &warp_ndfc_device.dev,
	}
};

static int warp_setup_nand_flash(void)
{
	struct device_node *np;

	/* Try to detect a rev A based on NOR size. */
	np = of_find_compatible_node(NULL, NULL, "cfi-flash");
	if (np) {
		struct property *pp;

		pp = of_find_property(np, "reg", NULL);
		if (pp && (pp->length == 12)) {
			u32 *v = pp->value;
			if (v[2] == 0x4000000) {
				/* Rev A = 64M NAND */
				warp_nand_chip0.nr_partitions = 3;

				nand_parts[1].size   = 0x3000000;
				nand_parts[2].offset = 0x3200000;
				nand_parts[2].size   = 0x0e00000;
			}
		}
		of_node_put(np);
	}

	platform_device_register(&warp_ndfc_device);
	platform_device_register(&warp_nand_device);

	return 0;
}
machine_device_initcall(warp, warp_setup_nand_flash);

#endif
