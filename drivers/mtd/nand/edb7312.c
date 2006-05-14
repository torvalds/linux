/*
 *  drivers/mtd/nand/edb7312.c
 *
 *  Copyright (C) 2002 Marius Gröger (mag@sysgo.de)
 *
 *  Derived from drivers/mtd/nand/autcpu12.c
 *       Copyright (c) 2001 Thomas Gleixner (gleixner@autronix.de)
 *
 * $Id: edb7312.c,v 1.12 2005/11/07 11:14:30 gleixner Exp $
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 *  Overview:
 *   This is a device driver for the NAND flash device found on the
 *   CLEP7312 board which utilizes the Toshiba TC58V64AFT part. This is
 *   a 64Mibit (8MiB x 8 bits) NAND flash device.
 */

#include <linux/slab.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/mtd/mtd.h>
#include <linux/mtd/nand.h>
#include <linux/mtd/partitions.h>
#include <asm/io.h>
#include <asm/arch/hardware.h>	/* for CLPS7111_VIRT_BASE */
#include <asm/sizes.h>
#include <asm/hardware/clps7111.h>

/*
 * MTD structure for EDB7312 board
 */
static struct mtd_info *ep7312_mtd = NULL;

/*
 * Values specific to the EDB7312 board (used with EP7312 processor)
 */
#define EP7312_FIO_PBASE 0x10000000	/* Phys address of flash */
#define EP7312_PXDR	0x0001	/*
				 * IO offset to Port B data register
				 * where the CLE, ALE and NCE pins
				 * are wired to.
				 */
#define EP7312_PXDDR	0x0041	/*
				 * IO offset to Port B data direction
				 * register so we can control the IO
				 * lines.
				 */

/*
 * Module stuff
 */

static unsigned long ep7312_fio_pbase = EP7312_FIO_PBASE;
static void __iomem *ep7312_pxdr = (void __iomem *)EP7312_PXDR;
static void __iomem *ep7312_pxddr = (void __iomem *)EP7312_PXDDR;

#ifdef CONFIG_MTD_PARTITIONS
/*
 * Define static partitions for flash device
 */
static struct mtd_partition partition_info[] = {
	{.name = "EP7312 Nand Flash",
	 .offset = 0,
	 .size = 8 * 1024 * 1024}
};

#define NUM_PARTITIONS 1

#endif

/*
 *	hardware specific access to control-lines
 */
static void ep7312_hwcontrol(struct mtd_info *mtd, int cmd)
{
	switch (cmd) {

	case NAND_CTL_SETCLE:
		clps_writeb(clps_readb(ep7312_pxdr) | 0x10, ep7312_pxdr);
		break;
	case NAND_CTL_CLRCLE:
		clps_writeb(clps_readb(ep7312_pxdr) & ~0x10, ep7312_pxdr);
		break;

	case NAND_CTL_SETALE:
		clps_writeb(clps_readb(ep7312_pxdr) | 0x20, ep7312_pxdr);
		break;
	case NAND_CTL_CLRALE:
		clps_writeb(clps_readb(ep7312_pxdr) & ~0x20, ep7312_pxdr);
		break;

	case NAND_CTL_SETNCE:
		clps_writeb((clps_readb(ep7312_pxdr) | 0x80) & ~0x40, ep7312_pxdr);
		break;
	case NAND_CTL_CLRNCE:
		clps_writeb((clps_readb(ep7312_pxdr) | 0x80) | 0x40, ep7312_pxdr);
		break;
	}
}

/*
 *	read device ready pin
 */
static int ep7312_device_ready(struct mtd_info *mtd)
{
	return 1;
}

#ifdef CONFIG_MTD_PARTITIONS
const char *part_probes[] = { "cmdlinepart", NULL };
#endif

/*
 * Main initialization routine
 */
static int __init ep7312_init(void)
{
	struct nand_chip *this;
	const char *part_type = 0;
	int mtd_parts_nb = 0;
	struct mtd_partition *mtd_parts = 0;
	void __iomem *ep7312_fio_base;

	/* Allocate memory for MTD device structure and private data */
	ep7312_mtd = kmalloc(sizeof(struct mtd_info) + sizeof(struct nand_chip), GFP_KERNEL);
	if (!ep7312_mtd) {
		printk("Unable to allocate EDB7312 NAND MTD device structure.\n");
		return -ENOMEM;
	}

	/* map physical adress */
	ep7312_fio_base = ioremap(ep7312_fio_pbase, SZ_1K);
	if (!ep7312_fio_base) {
		printk("ioremap EDB7312 NAND flash failed\n");
		kfree(ep7312_mtd);
		return -EIO;
	}

	/* Get pointer to private data */
	this = (struct nand_chip *)(&ep7312_mtd[1]);

	/* Initialize structures */
	memset(ep7312_mtd, 0, sizeof(struct mtd_info));
	memset(this, 0, sizeof(struct nand_chip));

	/* Link the private data with the MTD structure */
	ep7312_mtd->priv = this;
	ep7312_mtd->owner = THIS_MODULE;

	/*
	 * Set GPIO Port B control register so that the pins are configured
	 * to be outputs for controlling the NAND flash.
	 */
	clps_writeb(0xf0, ep7312_pxddr);

	/* insert callbacks */
	this->IO_ADDR_R = ep7312_fio_base;
	this->IO_ADDR_W = ep7312_fio_base;
	this->hwcontrol = ep7312_hwcontrol;
	this->dev_ready = ep7312_device_ready;
	/* 15 us command delay time */
	this->chip_delay = 15;

	/* Scan to find existence of the device */
	if (nand_scan(ep7312_mtd, 1)) {
		iounmap((void *)ep7312_fio_base);
		kfree(ep7312_mtd);
		return -ENXIO;
	}
#ifdef CONFIG_MTD_PARTITIONS
	ep7312_mtd->name = "edb7312-nand";
	mtd_parts_nb = parse_mtd_partitions(ep7312_mtd, part_probes, &mtd_parts, 0);
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
	add_mtd_partitions(ep7312_mtd, mtd_parts, mtd_parts_nb);

	/* Return happy */
	return 0;
}

module_init(ep7312_init);

/*
 * Clean up routine
 */
static void __exit ep7312_cleanup(void)
{
	struct nand_chip *this = (struct nand_chip *)&ep7312_mtd[1];

	/* Release resources, unregister device */
	nand_release(ap7312_mtd);

	/* Free internal data buffer */
	kfree(this->data_buf);

	/* Free the MTD device structure */
	kfree(ep7312_mtd);
}

module_exit(ep7312_cleanup);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Marius Groeger <mag@sysgo.de>");
MODULE_DESCRIPTION("MTD map driver for Cogent EDB7312 board");
