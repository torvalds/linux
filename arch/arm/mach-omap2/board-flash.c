/*
 * board-sdp-flash.c
 * Modified from mach-omap2/board-3430sdp-flash.c
 *
 * Copyright (C) 2009 Nokia Corporation
 * Copyright (C) 2009 Texas Instruments
 *
 * Vimal Singh <vimalsingh@ti.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/kernel.h>
#include <linux/platform_device.h>
#include <linux/mtd/physmap.h>
#include <linux/io.h>

#include <plat/gpmc.h>
#include <plat/nand.h>
#include <plat/onenand.h>
#include <plat/tc.h>

#include "board-flash.h"

#define REG_FPGA_REV			0x10
#define REG_FPGA_DIP_SWITCH_INPUT2	0x60
#define MAX_SUPPORTED_GPMC_CONFIG	3

#define DEBUG_BASE		0x08000000 /* debug board */

/* various memory sizes */
#define FLASH_SIZE_SDPV1	SZ_64M	/* NOR flash (64 Meg aligned) */
#define FLASH_SIZE_SDPV2	SZ_128M	/* NOR flash (256 Meg aligned) */

static struct physmap_flash_data board_nor_data = {
	.width		= 2,
};

static struct resource board_nor_resource = {
	.flags		= IORESOURCE_MEM,
};

static struct platform_device board_nor_device = {
	.name		= "physmap-flash",
	.id		= 0,
	.dev		= {
			.platform_data = &board_nor_data,
	},
	.num_resources	= 1,
	.resource	= &board_nor_resource,
};

static void
__init board_nor_init(struct mtd_partition *nor_parts, u8 nr_parts, u8 cs)
{
	int err;

	board_nor_data.parts	= nor_parts;
	board_nor_data.nr_parts	= nr_parts;

	/* Configure start address and size of NOR device */
	if (omap_rev() >= OMAP3430_REV_ES1_0) {
		err = gpmc_cs_request(cs, FLASH_SIZE_SDPV2 - 1,
				(unsigned long *)&board_nor_resource.start);
		board_nor_resource.end = board_nor_resource.start
					+ FLASH_SIZE_SDPV2 - 1;
	} else {
		err = gpmc_cs_request(cs, FLASH_SIZE_SDPV1 - 1,
				(unsigned long *)&board_nor_resource.start);
		board_nor_resource.end = board_nor_resource.start
					+ FLASH_SIZE_SDPV1 - 1;
	}
	if (err < 0) {
		printk(KERN_ERR "NOR: Can't request GPMC CS\n");
		return;
	}
	if (platform_device_register(&board_nor_device) < 0)
		printk(KERN_ERR	"Unable to register NOR device\n");
}

#if defined(CONFIG_MTD_ONENAND_OMAP2) || \
		defined(CONFIG_MTD_ONENAND_OMAP2_MODULE)
static struct omap_onenand_platform_data board_onenand_data = {
	.dma_channel	= -1,   /* disable DMA in OMAP OneNAND driver */
};

static void
__init board_onenand_init(struct mtd_partition *onenand_parts,
				u8 nr_parts, u8 cs)
{
	board_onenand_data.cs		= cs;
	board_onenand_data.parts	= onenand_parts;
	board_onenand_data.nr_parts	= nr_parts;

	gpmc_onenand_init(&board_onenand_data);
}
#else
static void
__init board_onenand_init(struct mtd_partition *nor_parts, u8 nr_parts, u8 cs)
{
}
#endif /* CONFIG_MTD_ONENAND_OMAP2 || CONFIG_MTD_ONENAND_OMAP2_MODULE */

#if defined(CONFIG_MTD_NAND_OMAP2) || \
		defined(CONFIG_MTD_NAND_OMAP2_MODULE)

/* Note that all values in this struct are in nanoseconds */
static struct gpmc_timings nand_timings = {

	.sync_clk = 0,

	.cs_on = 0,
	.cs_rd_off = 36,
	.cs_wr_off = 36,

	.adv_on = 6,
	.adv_rd_off = 24,
	.adv_wr_off = 36,

	.we_off = 30,
	.oe_off = 48,

	.access = 54,
	.rd_cycle = 72,
	.wr_cycle = 72,

	.wr_access = 30,
	.wr_data_mux_bus = 0,
};

static struct omap_nand_platform_data board_nand_data = {
	.nand_setup	= NULL,
	.gpmc_t		= &nand_timings,
	.dma_channel	= -1,		/* disable DMA in OMAP NAND driver */
	.dev_ready	= NULL,
	.devsize	= 0,	/* '0' for 8-bit, '1' for 16-bit device */
};

void
__init board_nand_init(struct mtd_partition *nand_parts, u8 nr_parts, u8 cs)
{
	board_nand_data.cs		= cs;
	board_nand_data.parts		= nand_parts;
	board_nand_data.nr_parts		= nr_parts;

	gpmc_nand_init(&board_nand_data);
}
#else
void
__init board_nand_init(struct mtd_partition *nand_parts, u8 nr_parts, u8 cs)
{
}
#endif /* CONFIG_MTD_NAND_OMAP2 || CONFIG_MTD_NAND_OMAP2_MODULE */

/**
 * get_gpmc0_type - Reads the FPGA DIP_SWITCH_INPUT_REGISTER2 to get
 * the various cs values.
 */
static u8 get_gpmc0_type(void)
{
	u8 cs = 0;
	void __iomem *fpga_map_addr;

	fpga_map_addr = ioremap(DEBUG_BASE, 4096);
	if (!fpga_map_addr)
		return -ENOMEM;

	if (!(__raw_readw(fpga_map_addr + REG_FPGA_REV)))
		/* we dont have an DEBUG FPGA??? */
		/* Depend on #defines!! default to strata boot return param */
		goto unmap;

	/* S8-DIP-OFF = 1, S8-DIP-ON = 0 */
	cs = __raw_readw(fpga_map_addr + REG_FPGA_DIP_SWITCH_INPUT2) & 0xf;

	/* ES2.0 SDP's onwards 4 dip switches are provided for CS */
	if (omap_rev() >= OMAP3430_REV_ES1_0)
		/* change (S8-1:4=DS-2:0) to (S8-4:1=DS-2:0) */
		cs = ((cs & 8) >> 3) | ((cs & 4) >> 1) |
			((cs & 2) << 1) | ((cs & 1) << 3);
	else
		/* change (S8-1:3=DS-2:0) to (S8-3:1=DS-2:0) */
		cs = ((cs & 4) >> 2) | (cs & 2) | ((cs & 1) << 2);
unmap:
	iounmap(fpga_map_addr);
	return cs;
}

/**
 * sdp3430_flash_init - Identify devices connected to GPMC and register.
 *
 * @return - void.
 */
void board_flash_init(struct flash_partitions partition_info[],
					char chip_sel_board[][GPMC_CS_NUM])
{
	u8		cs = 0;
	u8		norcs = GPMC_CS_NUM + 1;
	u8		nandcs = GPMC_CS_NUM + 1;
	u8		onenandcs = GPMC_CS_NUM + 1;
	u8		idx;
	unsigned char	*config_sel = NULL;

	/* REVISIT: Is this return correct idx for 2430 SDP?
	 * for which cs configuration matches for 2430 SDP?
	 */
	idx = get_gpmc0_type();
	if (idx >= MAX_SUPPORTED_GPMC_CONFIG) {
		printk(KERN_ERR "%s: Invalid chip select: %d\n", __func__, cs);
		return;
	}
	config_sel = (unsigned char *)(chip_sel_board[idx]);

	while (cs < GPMC_CS_NUM) {
		switch (config_sel[cs]) {
		case PDC_NOR:
			if (norcs > GPMC_CS_NUM)
				norcs = cs;
			break;
		case PDC_NAND:
			if (nandcs > GPMC_CS_NUM)
				nandcs = cs;
			break;
		case PDC_ONENAND:
			if (onenandcs > GPMC_CS_NUM)
				onenandcs = cs;
			break;
		};
		cs++;
	}

	if (norcs > GPMC_CS_NUM)
		printk(KERN_INFO "NOR: Unable to find configuration "
				"in GPMC\n");
	else
		board_nor_init(partition_info[0].parts,
				partition_info[0].nr_parts, norcs);

	if (onenandcs > GPMC_CS_NUM)
		printk(KERN_INFO "OneNAND: Unable to find configuration "
				"in GPMC\n");
	else
		board_onenand_init(partition_info[1].parts,
					partition_info[1].nr_parts, onenandcs);

	if (nandcs > GPMC_CS_NUM)
		printk(KERN_INFO "NAND: Unable to find configuration "
				"in GPMC\n");
	else
		board_nand_init(partition_info[2].parts,
				partition_info[2].nr_parts, nandcs);
}
