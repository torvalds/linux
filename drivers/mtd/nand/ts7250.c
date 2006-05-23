/*
 * drivers/mtd/nand/ts7250.c
 *
 * Copyright (C) 2004 Technologic Systems (support@embeddedARM.com)
 *
 * Derived from drivers/mtd/nand/edb7312.c
 *   Copyright (C) 2004 Marius Gröger (mag@sysgo.de)
 *
 * Derived from drivers/mtd/nand/autcpu12.c
 *   Copyright (c) 2001 Thomas Gleixner (gleixner@autronix.de)
 *
 * $Id: ts7250.c,v 1.4 2004/12/30 22:02:07 joff Exp $
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * Overview:
 *   This is a device driver for the NAND flash device found on the
 *   TS-7250 board which utilizes a Samsung 32 Mbyte part.
 */

#include <linux/slab.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/mtd/mtd.h>
#include <linux/mtd/nand.h>
#include <linux/mtd/partitions.h>
#include <asm/io.h>
#include <asm/arch/hardware.h>
#include <asm/sizes.h>
#include <asm/mach-types.h>

/*
 * MTD structure for TS7250 board
 */
static struct mtd_info *ts7250_mtd = NULL;

#ifdef CONFIG_MTD_PARTITIONS
static const char *part_probes[] = { "cmdlinepart", NULL };

#define NUM_PARTITIONS 3

/*
 * Define static partitions for flash device
 */
static struct mtd_partition partition_info32[] = {
	{
		.name		= "TS-BOOTROM",
		.offset		= 0x00000000,
		.size		= 0x00004000,
	}, {
		.name		= "Linux",
		.offset		= 0x00004000,
		.size		= 0x01d00000,
	}, {
		.name		= "RedBoot",
		.offset		= 0x01d04000,
		.size		= 0x002fc000,
	},
};

/*
 * Define static partitions for flash device
 */
static struct mtd_partition partition_info128[] = {
	{
		.name		= "TS-BOOTROM",
		.offset		= 0x00000000,
		.size		= 0x00004000,
	}, {
		.name		= "Linux",
		.offset		= 0x00004000,
		.size		= 0x07d00000,
	}, {
		.name		= "RedBoot",
		.offset		= 0x07d04000,
		.size		= 0x002fc000,
	},
};
#endif


/*
 *	hardware specific access to control-lines
 */
static void ts7250_hwcontrol(struct mtd_info *mtd, int cmd)
{
	unsigned long ctrl = TS72XX_NAND_CONTROL_VIRT_BASE;

	switch (cmd) {
	case NAND_CTL_SETCLE:
		__raw_writeb(__raw_readb(ctrl) | 0x2, ctrl);
		break;
	case NAND_CTL_CLRCLE:
		__raw_writeb(__raw_readb(ctrl) & ~0x2, ctrl);
		break;
	case NAND_CTL_SETALE:
		__raw_writeb(__raw_readb(ctrl) | 0x1, ctrl);
		break;
	case NAND_CTL_CLRALE:
		__raw_writeb(__raw_readb(ctrl) & ~0x1, ctrl);
		break;
	case NAND_CTL_SETNCE:
		__raw_writeb(__raw_readb(ctrl) | 0x4, ctrl);
		break;
	case NAND_CTL_CLRNCE:
		__raw_writeb(__raw_readb(ctrl) & ~0x4, ctrl);
		break;
	}
}

/*
 *	read device ready pin
 */
static int ts7250_device_ready(struct mtd_info *mtd)
{
	return __raw_readb(TS72XX_NAND_BUSY_VIRT_BASE) & 0x20;
}

/*
 * Main initialization routine
 */
static int __init ts7250_init(void)
{
	struct nand_chip *this;
	const char *part_type = 0;
	int mtd_parts_nb = 0;
	struct mtd_partition *mtd_parts = 0;

	if (!machine_is_ts72xx() || board_is_ts7200())
		return -ENXIO;

	/* Allocate memory for MTD device structure and private data */
	ts7250_mtd = kmalloc(sizeof(struct mtd_info) + sizeof(struct nand_chip), GFP_KERNEL);
	if (!ts7250_mtd) {
		printk("Unable to allocate TS7250 NAND MTD device structure.\n");
		return -ENOMEM;
	}

	/* Get pointer to private data */
	this = (struct nand_chip *)(&ts7250_mtd[1]);

	/* Initialize structures */
	memset(ts7250_mtd, 0, sizeof(struct mtd_info));
	memset(this, 0, sizeof(struct nand_chip));

	/* Link the private data with the MTD structure */
	ts7250_mtd->priv = this;
	ts7250_mtd->owner = THIS_MODULE;

	/* insert callbacks */
	this->IO_ADDR_R = (void *)TS72XX_NAND_DATA_VIRT_BASE;
	this->IO_ADDR_W = (void *)TS72XX_NAND_DATA_VIRT_BASE;
	this->hwcontrol = ts7250_hwcontrol;
	this->dev_ready = ts7250_device_ready;
	this->chip_delay = 15;
	this->ecc.mode = NAND_ECC_SOFT;

	printk("Searching for NAND flash...\n");
	/* Scan to find existence of the device */
	if (nand_scan(ts7250_mtd, 1)) {
		kfree(ts7250_mtd);
		return -ENXIO;
	}
#ifdef CONFIG_MTD_PARTITIONS
	ts7250_mtd->name = "ts7250-nand";
	mtd_parts_nb = parse_mtd_partitions(ts7250_mtd, part_probes, &mtd_parts, 0);
	if (mtd_parts_nb > 0)
		part_type = "command line";
	else
		mtd_parts_nb = 0;
#endif
	if (mtd_parts_nb == 0) {
		mtd_parts = partition_info32;
		if (ts7250_mtd->size >= (128 * 0x100000))
			mtd_parts = partition_info128;
		mtd_parts_nb = NUM_PARTITIONS;
		part_type = "static";
	}

	/* Register the partitions */
	printk(KERN_NOTICE "Using %s partition definition\n", part_type);
	add_mtd_partitions(ts7250_mtd, mtd_parts, mtd_parts_nb);

	/* Return happy */
	return 0;
}

module_init(ts7250_init);

/*
 * Clean up routine
 */
static void __exit ts7250_cleanup(void)
{
	/* Unregister the device */
	del_mtd_device(ts7250_mtd);

	/* Free the MTD device structure */
	kfree(ts7250_mtd);
}

module_exit(ts7250_cleanup);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Jesse Off <joff@embeddedARM.com>");
MODULE_DESCRIPTION("MTD map driver for Technologic Systems TS-7250 board");
