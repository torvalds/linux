/*
 *  drivers/mtd/autcpu12.c
 *
 *  Copyright (c) 2002 Thomas Gleixner <tgxl@linutronix.de>
 *
 *  Derived from drivers/mtd/spia.c
 * 	 Copyright (C) 2000 Steven J. Hill (sjhill@realitydiluted.com)
 * 
 * $Id: autcpu12.c,v 1.22 2004/11/04 12:53:10 gleixner Exp $
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 *  Overview:
 *   This is a device driver for the NAND flash device found on the
 *   autronix autcpu12 board, which is a SmartMediaCard. It supports 
 *   16MiB, 32MiB and 64MiB cards.
 *
 *
 *	02-12-2002 TG	Cleanup of module params
 *
 *	02-20-2002 TG	adjusted for different rd/wr adress support
 *			added support for read device ready/busy line
 *			added page_cache
 *
 *	10-06-2002 TG	128K card support added
 */

#include <linux/version.h>
#include <linux/slab.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/mtd/mtd.h>
#include <linux/mtd/nand.h>
#include <linux/mtd/partitions.h>
#include <asm/io.h>
#include <asm/arch/hardware.h>
#include <asm/sizes.h>
#include <asm/arch/autcpu12.h>

/*
 * MTD structure for AUTCPU12 board
 */
static struct mtd_info *autcpu12_mtd = NULL;

static int autcpu12_io_base = CS89712_VIRT_BASE;
static int autcpu12_fio_pbase = AUTCPU12_PHYS_SMC;
static int autcpu12_fio_ctrl = AUTCPU12_SMC_SELECT_OFFSET;
static int autcpu12_pedr = AUTCPU12_SMC_PORT_OFFSET;
static void __iomem * autcpu12_fio_base;

/*
 * Define partitions for flash devices
 */
static struct mtd_partition partition_info16k[] = {
	{ .name		= "AUTCPU12 flash partition 1",
	  .offset	= 0,
	  .size		= 8 * SZ_1M },
	{ .name		= "AUTCPU12 flash partition 2",
	  .offset	= 8 * SZ_1M,
	  .size		= 8 * SZ_1M },
};

static struct mtd_partition partition_info32k[] = {
	{ .name		= "AUTCPU12 flash partition 1",
	  .offset	= 0,
	  .size		= 8 * SZ_1M },
	{ .name		= "AUTCPU12 flash partition 2",
	  .offset	= 8 * SZ_1M,
	  .size		= 24 * SZ_1M },
};

static struct mtd_partition partition_info64k[] = {
	{ .name		= "AUTCPU12 flash partition 1",
	  .offset	= 0,
	  .size		= 16 * SZ_1M },
	{ .name		= "AUTCPU12 flash partition 2",
	  .offset	= 16 * SZ_1M,
	  .size		= 48 * SZ_1M },
};

static struct mtd_partition partition_info128k[] = {
	{ .name		= "AUTCPU12 flash partition 1",
	  .offset	= 0,
	  .size		= 16 * SZ_1M },
	{ .name		= "AUTCPU12 flash partition 2",
	  .offset	= 16 * SZ_1M,
	  .size		= 112 * SZ_1M },
};

#define NUM_PARTITIONS16K 2
#define NUM_PARTITIONS32K 2
#define NUM_PARTITIONS64K 2
#define NUM_PARTITIONS128K 2
/* 
 *	hardware specific access to control-lines
*/
static void autcpu12_hwcontrol(struct mtd_info *mtd, int cmd)
{

	switch(cmd){

		case NAND_CTL_SETCLE: (*(volatile unsigned char *) (autcpu12_io_base + autcpu12_pedr)) |=  AUTCPU12_SMC_CLE; break;
		case NAND_CTL_CLRCLE: (*(volatile unsigned char *) (autcpu12_io_base + autcpu12_pedr)) &= ~AUTCPU12_SMC_CLE; break;

		case NAND_CTL_SETALE: (*(volatile unsigned char *) (autcpu12_io_base + autcpu12_pedr)) |=  AUTCPU12_SMC_ALE; break;
		case NAND_CTL_CLRALE: (*(volatile unsigned char *) (autcpu12_io_base + autcpu12_pedr)) &= ~AUTCPU12_SMC_ALE; break;

		case NAND_CTL_SETNCE: (*(volatile unsigned char *) (autcpu12_fio_base + autcpu12_fio_ctrl)) = 0x01; break;
		case NAND_CTL_CLRNCE: (*(volatile unsigned char *) (autcpu12_fio_base + autcpu12_fio_ctrl)) = 0x00; break;
	}
}

/*
*	read device ready pin
*/
int autcpu12_device_ready(struct mtd_info *mtd)
{

	return ( (*(volatile unsigned char *) (autcpu12_io_base + autcpu12_pedr)) & AUTCPU12_SMC_RDY) ? 1 : 0;

}

/*
 * Main initialization routine
 */
int __init autcpu12_init (void)
{
	struct nand_chip *this;
	int err = 0;

	/* Allocate memory for MTD device structure and private data */
	autcpu12_mtd = kmalloc (sizeof(struct mtd_info) + sizeof (struct nand_chip),
				GFP_KERNEL);
	if (!autcpu12_mtd) {
		printk ("Unable to allocate AUTCPU12 NAND MTD device structure.\n");
		err = -ENOMEM;
		goto out;
	}

	/* map physical adress */
	autcpu12_fio_base = ioremap(autcpu12_fio_pbase,SZ_1K);
	if(!autcpu12_fio_base){
		printk("Ioremap autcpu12 SmartMedia Card failed\n");
		err = -EIO;
		goto out_mtd;
	}

	/* Get pointer to private data */
	this = (struct nand_chip *) (&autcpu12_mtd[1]);

	/* Initialize structures */
	memset((char *) autcpu12_mtd, 0, sizeof(struct mtd_info));
	memset((char *) this, 0, sizeof(struct nand_chip));

	/* Link the private data with the MTD structure */
	autcpu12_mtd->priv = this;

	/* Set address of NAND IO lines */
	this->IO_ADDR_R = autcpu12_fio_base;
	this->IO_ADDR_W = autcpu12_fio_base;
	this->hwcontrol = autcpu12_hwcontrol;
	this->dev_ready = autcpu12_device_ready;
	/* 20 us command delay time */
	this->chip_delay = 20;		
	this->eccmode = NAND_ECC_SOFT;

	/* Enable the following for a flash based bad block table */
	/*
	this->options = NAND_USE_FLASH_BBT;
	*/
	this->options = NAND_USE_FLASH_BBT;
	
	/* Scan to find existance of the device */
	if (nand_scan (autcpu12_mtd, 1)) {
		err = -ENXIO;
		goto out_ior;
	}
	
	/* Register the partitions */
	switch(autcpu12_mtd->size){
		case SZ_16M: add_mtd_partitions(autcpu12_mtd, partition_info16k, NUM_PARTITIONS16K); break;
		case SZ_32M: add_mtd_partitions(autcpu12_mtd, partition_info32k, NUM_PARTITIONS32K); break;
		case SZ_64M: add_mtd_partitions(autcpu12_mtd, partition_info64k, NUM_PARTITIONS64K); break; 
		case SZ_128M: add_mtd_partitions(autcpu12_mtd, partition_info128k, NUM_PARTITIONS128K); break; 
		default: {
			printk ("Unsupported SmartMedia device\n"); 
			err = -ENXIO;
			goto out_ior;
		}
	}
	goto out;

out_ior:
	iounmap((void *)autcpu12_fio_base);
out_mtd:
	kfree (autcpu12_mtd);
out:
	return err;
}

module_init(autcpu12_init);

/*
 * Clean up routine
 */
#ifdef MODULE
static void __exit autcpu12_cleanup (void)
{
	/* Release resources, unregister device */
	nand_release (autcpu12_mtd);

	/* unmap physical adress */
	iounmap((void *)autcpu12_fio_base);
	
	/* Free the MTD device structure */
	kfree (autcpu12_mtd);
}
module_exit(autcpu12_cleanup);
#endif

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Thomas Gleixner <tglx@linutronix.de>");
MODULE_DESCRIPTION("Glue layer for SmartMediaCard on autronix autcpu12");
