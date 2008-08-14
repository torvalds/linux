/*
 *  drivers/mtd/nand/h1910.c
 *
 *  Copyright (C) 2003 Joshua Wise (joshua@joshuawise.com)
 *
 *  Derived from drivers/mtd/nand/edb7312.c
 *       Copyright (C) 2002 Marius Gröger (mag@sysgo.de)
 *       Copyright (c) 2001 Thomas Gleixner (gleixner@autronix.de)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 *  Overview:
 *   This is a device driver for the NAND flash device found on the
 *   iPAQ h1910 board which utilizes the Samsung K9F2808 part. This is
 *   a 128Mibit (16MiB x 8 bits) NAND flash device.
 */

#include <linux/slab.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/mtd/mtd.h>
#include <linux/mtd/nand.h>
#include <linux/mtd/partitions.h>
#include <asm/io.h>
#include <mach/hardware.h>	/* for CLPS7111_VIRT_BASE */
#include <asm/sizes.h>
#include <mach/h1900-gpio.h>
#include <mach/ipaq.h>

/*
 * MTD structure for EDB7312 board
 */
static struct mtd_info *h1910_nand_mtd = NULL;

/*
 * Module stuff
 */

#ifdef CONFIG_MTD_PARTITIONS
/*
 * Define static partitions for flash device
 */
static struct mtd_partition partition_info[] = {
      {name:"h1910 NAND Flash",
	      offset:0,
      size:16 * 1024 * 1024}
};

#define NUM_PARTITIONS 1

#endif

/*
 *	hardware specific access to control-lines
 *
 *	NAND_NCE: bit 0 - don't care
 *	NAND_CLE: bit 1 - address bit 2
 *	NAND_ALE: bit 2 - address bit 3
 */
static void h1910_hwcontrol(struct mtd_info *mtd, int cmd,
			    unsigned int ctrl)
{
	struct nand_chip *chip = mtd->priv;

	if (cmd != NAND_CMD_NONE)
		writeb(cmd, chip->IO_ADDR_W | ((ctrl & 0x6) << 1));
}

/*
 *	read device ready pin
 */
#if 0
static int h1910_device_ready(struct mtd_info *mtd)
{
	return (GPLR(55) & GPIO_bit(55));
}
#endif

/*
 * Main initialization routine
 */
static int __init h1910_init(void)
{
	struct nand_chip *this;
	const char *part_type = 0;
	int mtd_parts_nb = 0;
	struct mtd_partition *mtd_parts = 0;
	void __iomem *nandaddr;

	if (!machine_is_h1900())
		return -ENODEV;

	nandaddr = ioremap(0x08000000, 0x1000);
	if (!nandaddr) {
		printk("Failed to ioremap nand flash.\n");
		return -ENOMEM;
	}

	/* Allocate memory for MTD device structure and private data */
	h1910_nand_mtd = kmalloc(sizeof(struct mtd_info) + sizeof(struct nand_chip), GFP_KERNEL);
	if (!h1910_nand_mtd) {
		printk("Unable to allocate h1910 NAND MTD device structure.\n");
		iounmap((void *)nandaddr);
		return -ENOMEM;
	}

	/* Get pointer to private data */
	this = (struct nand_chip *)(&h1910_nand_mtd[1]);

	/* Initialize structures */
	memset(h1910_nand_mtd, 0, sizeof(struct mtd_info));
	memset(this, 0, sizeof(struct nand_chip));

	/* Link the private data with the MTD structure */
	h1910_nand_mtd->priv = this;
	h1910_nand_mtd->owner = THIS_MODULE;

	/*
	 * Enable VPEN
	 */
	GPSR(37) = GPIO_bit(37);

	/* insert callbacks */
	this->IO_ADDR_R = nandaddr;
	this->IO_ADDR_W = nandaddr;
	this->cmd_ctrl = h1910_hwcontrol;
	this->dev_ready = NULL;	/* unknown whether that was correct or not so we will just do it like this */
	/* 15 us command delay time */
	this->chip_delay = 50;
	this->ecc.mode = NAND_ECC_SOFT;
	this->options = NAND_NO_AUTOINCR;

	/* Scan to find existence of the device */
	if (nand_scan(h1910_nand_mtd, 1)) {
		printk(KERN_NOTICE "No NAND device - returning -ENXIO\n");
		kfree(h1910_nand_mtd);
		iounmap((void *)nandaddr);
		return -ENXIO;
	}
#ifdef CONFIG_MTD_CMDLINE_PARTS
	mtd_parts_nb = parse_cmdline_partitions(h1910_nand_mtd, &mtd_parts, "h1910-nand");
	if (mtd_parts_nb > 0)
		part_type = "command line";
	else
		mtd_parts_nb = 0;
#endif
	if (mtd_parts_nb == 0) {
		mtd_parts = partition_info;
		mtd_parts_nb = NUM_PARTITIONS;
		part_type = "static";
	}

	/* Register the partitions */
	printk(KERN_NOTICE "Using %s partition definition\n", part_type);
	add_mtd_partitions(h1910_nand_mtd, mtd_parts, mtd_parts_nb);

	/* Return happy */
	return 0;
}

module_init(h1910_init);

/*
 * Clean up routine
 */
static void __exit h1910_cleanup(void)
{
	struct nand_chip *this = (struct nand_chip *)&h1910_nand_mtd[1];

	/* Release resources, unregister device */
	nand_release(h1910_nand_mtd);

	/* Release io resource */
	iounmap((void *)this->IO_ADDR_W);

	/* Free the MTD device structure */
	kfree(h1910_nand_mtd);
}

module_exit(h1910_cleanup);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Joshua Wise <joshua at joshuawise dot com>");
MODULE_DESCRIPTION("NAND flash driver for iPAQ h1910");
