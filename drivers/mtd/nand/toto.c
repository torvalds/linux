/*
 *  drivers/mtd/nand/toto.c
 *
 *  Copyright (c) 2003 Texas Instruments
 *
 *  Derived from drivers/mtd/autcpu12.c
 *
 *  Copyright (c) 2002 Thomas Gleixner <tgxl@linutronix.de>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 *  Overview:
 *   This is a device driver for the NAND flash device found on the
 *   TI fido board. It supports 32MiB and 64MiB cards
 *
 * $Id: toto.c,v 1.5 2005/11/07 11:14:31 gleixner Exp $
 */

#include <linux/slab.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/delay.h>
#include <linux/mtd/mtd.h>
#include <linux/mtd/nand.h>
#include <linux/mtd/partitions.h>
#include <asm/io.h>
#include <asm/arch/hardware.h>
#include <asm/sizes.h>
#include <asm/arch/toto.h>
#include <asm/arch-omap1510/hardware.h>
#include <asm/arch/gpio.h>

#define CONFIG_NAND_WORKAROUND 1

/*
 * MTD structure for TOTO board
 */
static struct mtd_info *toto_mtd = NULL;

static unsigned long toto_io_base = OMAP_FLASH_1_BASE;

/*
 * Define partitions for flash devices
 */

static struct mtd_partition partition_info64M[] = {
	{ .name =	"toto kernel partition 1",
	  .offset =	0,
	  .size	=	2 * SZ_1M },
	{ .name =	"toto file sys partition 2",
	  .offset =	2 * SZ_1M,
	  .size =	14 * SZ_1M },
	{ .name =	"toto user partition 3",
	  .offset =	16 * SZ_1M,
	  .size =	16 * SZ_1M },
	{ .name =	"toto devboard extra partition 4",
	  .offset =	32 * SZ_1M,
	  .size =	32 * SZ_1M },
};

static struct mtd_partition partition_info32M[] = {
	{ .name =	"toto kernel partition 1",
	  .offset =	0,
	  .size =	2 * SZ_1M },
	{ .name =	"toto file sys partition 2",
	  .offset =	2 * SZ_1M,
	  .size =	14 * SZ_1M },
	{ .name =	"toto user partition 3",
	  .offset =	16 * SZ_1M,
	  .size =	16 * SZ_1M },
};

#define NUM_PARTITIONS32M 3
#define NUM_PARTITIONS64M 4

/*
 *	hardware specific access to control-lines
 *
 *	ctrl:
 *	NAND_NCE: bit 0 -> bit 14 (0x4000)
 *	NAND_CLE: bit 1 -> bit 12 (0x1000)
 *	NAND_ALE: bit 2 -> bit 1  (0x0002)
 */
static void toto_hwcontrol(struct mtd_info *mtd, int cmd,
			   unsigned int ctrl)
{
	struct nand_chip *chip = mtd->priv;

	if (ctrl & NAND_CTRL_CHANGE) {
		unsigned long bits;

		/* hopefully enough time for tc make proceding write to clear */
		udelay(1);

		bits = (~ctrl & NAND_NCE) << 14;
		bits |= (ctrl & NAND_CLE) << 12;
		bits |= (ctrl & NAND_ALE) >> 1;

#warning Wild guess as gpiosetout() is nowhere defined in the kernel source - tglx
		gpiosetout(0x5002, bits);

#ifdef CONFIG_NAND_WORKAROUND
		/* "some" dev boards busted, blue wired to rts2 :( */
		rts2setout(2, (ctrl & NAND_CLE) << 1);
#endif
		/* allow time to ensure gpio state to over take memory write */
		udelay(1);
	}

	if (cmd != NAND_CMD_NONE)
		writeb(cmd, chip->IO_ADDR_W);
}

/*
 * Main initialization routine
 */
static int __init toto_init(void)
{
	struct nand_chip *this;
	int err = 0;

	/* Allocate memory for MTD device structure and private data */
	toto_mtd = kmalloc(sizeof(struct mtd_info) + sizeof(struct nand_chip), GFP_KERNEL);
	if (!toto_mtd) {
		printk(KERN_WARNING "Unable to allocate toto NAND MTD device structure.\n");
		err = -ENOMEM;
		goto out;
	}

	/* Get pointer to private data */
	this = (struct nand_chip *)(&toto_mtd[1]);

	/* Initialize structures */
	memset(toto_mtd, 0, sizeof(struct mtd_info));
	memset(this, 0, sizeof(struct nand_chip));

	/* Link the private data with the MTD structure */
	toto_mtd->priv = this;
	toto_mtd->owner = THIS_MODULE;

	/* Set address of NAND IO lines */
	this->IO_ADDR_R = toto_io_base;
	this->IO_ADDR_W = toto_io_base;
	this->cmd_ctrl = toto_hwcontrol;
	this->dev_ready = NULL;
	/* 25 us command delay time */
	this->chip_delay = 30;
	this->ecc.mode = NAND_ECC_SOFT;

	/* Scan to find existance of the device */
	if (nand_scan(toto_mtd, 1)) {
		err = -ENXIO;
		goto out_mtd;
	}

	/* Register the partitions */
	switch (toto_mtd->size) {
	case SZ_64M:
		add_mtd_partitions(toto_mtd, partition_info64M, NUM_PARTITIONS64M);
		break;
	case SZ_32M:
		add_mtd_partitions(toto_mtd, partition_info32M, NUM_PARTITIONS32M);
		break;
	default:{
			printk(KERN_WARNING "Unsupported Nand device\n");
			err = -ENXIO;
			goto out_buf;
		}
	}

	gpioreserve(NAND_MASK);	/* claim our gpios */
	archflashwp(0, 0);	/* open up flash for writing */

	goto out;

 out_buf:
	kfree(this->data_buf);
 out_mtd:
	kfree(toto_mtd);
 out:
	return err;
}

module_init(toto_init);

/*
 * Clean up routine
 */
static void __exit toto_cleanup(void)
{
	/* Release resources, unregister device */
	nand_release(toto_mtd);

	/* Free the MTD device structure */
	kfree(toto_mtd);

	/* stop flash writes */
	archflashwp(0, 1);

	/* release gpios to system */
	gpiorelease(NAND_MASK);
}

module_exit(toto_cleanup);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Richard Woodruff <r-woodruff2@ti.com>");
MODULE_DESCRIPTION("Glue layer for NAND flash on toto board");
