/*
 * drivers/mtd/nand/tx4938ndfmc.c
 *
 *  Overview:
 *   This is a device driver for the NAND flash device connected to
 *   TX4938 internal NAND Memory Controller.
 *   TX4938 NDFMC is almost same as TX4925 NDFMC, but register size are 64 bit.
 *
 * Author: source@mvista.com
 *
 * Based on spia.c by Steven J. Hill
 *
 * $Id: tx4938ndfmc.c,v 1.4 2004/10/05 13:50:20 gleixner Exp $
 *
 * Copyright (C) 2000-2001 Toshiba Corporation 
 *
 * 2003 (c) MontaVista Software, Inc. This file is licensed under the
 * terms of the GNU General Public License version 2. This program is
 * licensed "as is" without any warranty of any kind, whether express
 * or implied.
 */
#include <linux/config.h>
#include <linux/slab.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/mtd/mtd.h>
#include <linux/mtd/nand.h>
#include <linux/mtd/nand_ecc.h>
#include <linux/mtd/partitions.h>
#include <asm/io.h>
#include <asm/bootinfo.h>
#include <linux/delay.h>
#include <asm/tx4938/rbtx4938.h>

extern struct nand_oobinfo jffs2_oobinfo;

/*
 * MTD structure for TX4938 NDFMC
 */
static struct mtd_info *tx4938ndfmc_mtd;

/*
 * Define partitions for flash device
 */
#define flush_wb()	(void)tx4938_ndfmcptr->mcr;

#define NUM_PARTITIONS  	3
#define NUMBER_OF_CIS_BLOCKS	24
#define SIZE_OF_BLOCK		0x00004000
#define NUMBER_OF_BLOCK_PER_ZONE 1024
#define SIZE_OF_ZONE		(NUMBER_OF_BLOCK_PER_ZONE * SIZE_OF_BLOCK)
#ifndef CONFIG_MTD_CMDLINE_PARTS
/*
 * You can use the following sample of MTD partitions 
 * on the NAND Flash Memory 32MB or more.
 *
 * The following figure shows the image of the sample partition on
 * the 32MB NAND Flash Memory. 
 *
 *   Block No.
 *    0 +-----------------------------+ ------
 *      |             CIS             |   ^
 *   24 +-----------------------------+   |
 *      |         kernel image        |   | Zone 0
 *      |                             |   |
 *      +-----------------------------+   |
 * 1023 |         unused area         |   v
 *      +-----------------------------+ ------
 * 1024 |            JFFS2            |   ^
 *      |                             |   |
 *      |                             |   | Zone 1
 *      |                             |   |
 *      |                             |   |
 *      |                             |   v
 * 2047 +-----------------------------+ ------
 *
 */
static struct mtd_partition partition_info[NUM_PARTITIONS] = {
	{
		.name = "RBTX4938 CIS Area",
 		.offset =  0,
 		.size =    (NUMBER_OF_CIS_BLOCKS * SIZE_OF_BLOCK),
 		.mask_flags  = MTD_WRITEABLE	/* This partition is NOT writable */
 	},
 	{
 		.name = "RBTX4938 kernel image",
 		.offset =  MTDPART_OFS_APPEND,
 		.size =    8 * 0x00100000,	/* 8MB (Depends on size of kernel image) */
 		.mask_flags  = MTD_WRITEABLE	/* This partition is NOT writable */
 	},
 	{
 		.name = "Root FS (JFFS2)",
 		.offset =  (0 + SIZE_OF_ZONE),    /* start address of next zone */
 		.size =    MTDPART_SIZ_FULL
 	},
};
#endif

static void tx4938ndfmc_hwcontrol(struct mtd_info *mtd, int cmd)
{
	switch (cmd) {
		case NAND_CTL_SETCLE:
			tx4938_ndfmcptr->mcr |= TX4938_NDFMCR_CLE;
			break;
		case NAND_CTL_CLRCLE:
			tx4938_ndfmcptr->mcr &= ~TX4938_NDFMCR_CLE;
			break;
		case NAND_CTL_SETALE:
			tx4938_ndfmcptr->mcr |= TX4938_NDFMCR_ALE;
			break;
		case NAND_CTL_CLRALE:
			tx4938_ndfmcptr->mcr &= ~TX4938_NDFMCR_ALE;
			break;
		/* TX4938_NDFMCR_CE bit is 0:high 1:low */
		case NAND_CTL_SETNCE:
			tx4938_ndfmcptr->mcr |= TX4938_NDFMCR_CE;
			break;
		case NAND_CTL_CLRNCE:
			tx4938_ndfmcptr->mcr &= ~TX4938_NDFMCR_CE;
			break;
		case NAND_CTL_SETWP:
			tx4938_ndfmcptr->mcr |= TX4938_NDFMCR_WE;
			break;
		case NAND_CTL_CLRWP:
			tx4938_ndfmcptr->mcr &= ~TX4938_NDFMCR_WE;
			break;
	}
}
static int tx4938ndfmc_dev_ready(struct mtd_info *mtd)
{
	flush_wb();
	return !(tx4938_ndfmcptr->sr & TX4938_NDFSR_BUSY);
}
static void tx4938ndfmc_calculate_ecc(struct mtd_info *mtd, const u_char *dat, u_char *ecc_code)
{
	u32 mcr = tx4938_ndfmcptr->mcr;
	mcr &= ~TX4938_NDFMCR_ECC_ALL;
	tx4938_ndfmcptr->mcr = mcr | TX4938_NDFMCR_ECC_OFF;
	tx4938_ndfmcptr->mcr = mcr | TX4938_NDFMCR_ECC_READ;
	ecc_code[1] = tx4938_ndfmcptr->dtr;
	ecc_code[0] = tx4938_ndfmcptr->dtr;
	ecc_code[2] = tx4938_ndfmcptr->dtr;
	tx4938_ndfmcptr->mcr = mcr | TX4938_NDFMCR_ECC_OFF;
}
static void tx4938ndfmc_enable_hwecc(struct mtd_info *mtd, int mode)
{
	u32 mcr = tx4938_ndfmcptr->mcr;
	mcr &= ~TX4938_NDFMCR_ECC_ALL;
	tx4938_ndfmcptr->mcr = mcr | TX4938_NDFMCR_ECC_RESET;
	tx4938_ndfmcptr->mcr = mcr | TX4938_NDFMCR_ECC_OFF;
	tx4938_ndfmcptr->mcr = mcr | TX4938_NDFMCR_ECC_ON;
}

static u_char tx4938ndfmc_nand_read_byte(struct mtd_info *mtd)
{
	struct nand_chip *this = mtd->priv;
	return tx4938_read_nfmc(this->IO_ADDR_R);
}

static void tx4938ndfmc_nand_write_byte(struct mtd_info *mtd, u_char byte)
{
	struct nand_chip *this = mtd->priv;
	tx4938_write_nfmc(byte, this->IO_ADDR_W);
}

static void tx4938ndfmc_nand_write_buf(struct mtd_info *mtd, const u_char *buf, int len)
{
	int i;
	struct nand_chip *this = mtd->priv;

	for (i=0; i<len; i++)
		tx4938_write_nfmc(buf[i], this->IO_ADDR_W);
}

static void tx4938ndfmc_nand_read_buf(struct mtd_info *mtd, u_char *buf, int len)
{
	int i;
	struct nand_chip *this = mtd->priv;

	for (i=0; i<len; i++)
		buf[i] = tx4938_read_nfmc(this->IO_ADDR_R);
}

static int tx4938ndfmc_nand_verify_buf(struct mtd_info *mtd, const u_char *buf, int len)
{
	int i;
	struct nand_chip *this = mtd->priv;

	for (i=0; i<len; i++)
		if (buf[i] != tx4938_read_nfmc(this->IO_ADDR_R))
			return -EFAULT;

	return 0;
}

/*
 * Send command to NAND device
 */
static void tx4938ndfmc_nand_command (struct mtd_info *mtd, unsigned command, int column, int page_addr)
{
	register struct nand_chip *this = mtd->priv;

	/* Begin command latch cycle */
	this->hwcontrol(mtd, NAND_CTL_SETCLE);
	/*
	 * Write out the command to the device.
	 */
	if (command == NAND_CMD_SEQIN) {
		int readcmd;

		if (column >= mtd->oobblock) {
			/* OOB area */
			column -= mtd->oobblock;
			readcmd = NAND_CMD_READOOB;
		} else if (column < 256) {
			/* First 256 bytes --> READ0 */
			readcmd = NAND_CMD_READ0;
		} else {
			column -= 256;
			readcmd = NAND_CMD_READ1;
		}
		this->write_byte(mtd, readcmd);
	}
	this->write_byte(mtd, command);

	/* Set ALE and clear CLE to start address cycle */
	this->hwcontrol(mtd, NAND_CTL_CLRCLE);

	if (column != -1 || page_addr != -1) {
		this->hwcontrol(mtd, NAND_CTL_SETALE);

		/* Serially input address */
		if (column != -1)
			this->write_byte(mtd, column);
		if (page_addr != -1) {
			this->write_byte(mtd, (unsigned char) (page_addr & 0xff));
			this->write_byte(mtd, (unsigned char) ((page_addr >> 8) & 0xff));
			/* One more address cycle for higher density devices */
			if (mtd->size & 0x0c000000) 
				this->write_byte(mtd, (unsigned char) ((page_addr >> 16) & 0x0f));
		}
		/* Latch in address */
		this->hwcontrol(mtd, NAND_CTL_CLRALE);
	}
	
	/* 
	 * program and erase have their own busy handlers 
	 * status and sequential in needs no delay
	*/
	switch (command) {
			
	case NAND_CMD_PAGEPROG:
		/* Turn off WE */
		this->hwcontrol (mtd, NAND_CTL_CLRWP);
                return;

	case NAND_CMD_SEQIN:
		/* Turn on WE */
		this->hwcontrol (mtd, NAND_CTL_SETWP);
                return;

	case NAND_CMD_ERASE1:
	case NAND_CMD_ERASE2:
	case NAND_CMD_STATUS:
		return;

	case NAND_CMD_RESET:
		if (this->dev_ready)	
			break;
		this->hwcontrol(mtd, NAND_CTL_SETCLE);
		this->write_byte(mtd, NAND_CMD_STATUS);
		this->hwcontrol(mtd, NAND_CTL_CLRCLE);
		while ( !(this->read_byte(mtd) & 0x40));
		return;

	/* This applies to read commands */	
	default:
		/* 
		 * If we don't have access to the busy pin, we apply the given
		 * command delay
		*/
		if (!this->dev_ready) {
			udelay (this->chip_delay);
			return;
		}	
	}
	
	/* wait until command is processed */
	while (!this->dev_ready(mtd));
}

#ifdef CONFIG_MTD_CMDLINE_PARTS
extern int parse_cmdline_partitions(struct mtd_info *master, struct mtd_partition **pparts, char *);
#endif
/*
 * Main initialization routine
 */
int __init tx4938ndfmc_init (void)
{
	struct nand_chip *this;
	int bsprt = 0, hold = 0xf, spw = 0xf;
	int protected = 0;

	if ((*rbtx4938_piosel_ptr & 0x0c) != 0x08) {
		printk("TX4938 NDFMC: disabled by IOC PIOSEL\n");
		return -ENODEV;
	}
	bsprt = 1;
	hold = 2;
	spw = 9 - 1;	/* 8 GBUSCLK = 80ns (@ GBUSCLK 100MHz) */

	if ((tx4938_ccfgptr->pcfg &
	     (TX4938_PCFG_ATA_SEL|TX4938_PCFG_ISA_SEL|TX4938_PCFG_NDF_SEL))
	    != TX4938_PCFG_NDF_SEL) {
		printk("TX4938 NDFMC: disabled by PCFG.\n");
		return -ENODEV;
	}

	/* reset NDFMC */
	tx4938_ndfmcptr->rstr |= TX4938_NDFRSTR_RST;
	while (tx4938_ndfmcptr->rstr & TX4938_NDFRSTR_RST)
		;
	/* setup BusSeparete, Hold Time, Strobe Pulse Width */
	tx4938_ndfmcptr->mcr = bsprt ? TX4938_NDFMCR_BSPRT : 0;
	tx4938_ndfmcptr->spr = hold << 4 | spw;

	/* Allocate memory for MTD device structure and private data */
	tx4938ndfmc_mtd = kmalloc (sizeof(struct mtd_info) + sizeof (struct nand_chip),
				      GFP_KERNEL);
	if (!tx4938ndfmc_mtd) {
		printk ("Unable to allocate TX4938 NDFMC MTD device structure.\n");
		return -ENOMEM;
	}

	/* Get pointer to private data */
	this = (struct nand_chip *) (&tx4938ndfmc_mtd[1]);

	/* Initialize structures */
	memset((char *) tx4938ndfmc_mtd, 0, sizeof(struct mtd_info));
	memset((char *) this, 0, sizeof(struct nand_chip));

	/* Link the private data with the MTD structure */
	tx4938ndfmc_mtd->priv = this;

	/* Set address of NAND IO lines */
	this->IO_ADDR_R = (unsigned long)&tx4938_ndfmcptr->dtr;
	this->IO_ADDR_W = (unsigned long)&tx4938_ndfmcptr->dtr;
	this->hwcontrol = tx4938ndfmc_hwcontrol;
	this->dev_ready = tx4938ndfmc_dev_ready;
	this->calculate_ecc = tx4938ndfmc_calculate_ecc;
	this->correct_data = nand_correct_data;
	this->enable_hwecc = tx4938ndfmc_enable_hwecc;
	this->eccmode = NAND_ECC_HW3_256;
	this->chip_delay = 100;
	this->read_byte = tx4938ndfmc_nand_read_byte;
	this->write_byte = tx4938ndfmc_nand_write_byte;
	this->cmdfunc = tx4938ndfmc_nand_command;
	this->write_buf = tx4938ndfmc_nand_write_buf;
	this->read_buf = tx4938ndfmc_nand_read_buf;
	this->verify_buf = tx4938ndfmc_nand_verify_buf;

	/* Scan to find existance of the device */
	if (nand_scan (tx4938ndfmc_mtd, 1)) {
		kfree (tx4938ndfmc_mtd);
		return -ENXIO;
	}

	if (protected) {
		printk(KERN_INFO "TX4938 NDFMC: write protected.\n");
		tx4938ndfmc_mtd->flags &= ~(MTD_WRITEABLE | MTD_ERASEABLE);
	}

#ifdef CONFIG_MTD_CMDLINE_PARTS
	{
		int mtd_parts_nb = 0;
		struct mtd_partition *mtd_parts = 0;
		mtd_parts_nb = parse_cmdline_partitions(tx4938ndfmc_mtd, &mtd_parts, "tx4938ndfmc");
		if (mtd_parts_nb > 0)
			add_mtd_partitions(tx4938ndfmc_mtd, mtd_parts, mtd_parts_nb);
		else
			add_mtd_device(tx4938ndfmc_mtd);
	}
#else
	add_mtd_partitions(tx4938ndfmc_mtd, partition_info, NUM_PARTITIONS );
#endif

	return 0;
}
module_init(tx4938ndfmc_init);

/*
 * Clean up routine
 */
static void __exit tx4938ndfmc_cleanup (void)
{
	/* Release resources, unregister device */
	nand_release (tx4938ndfmc_mtd);

	/* Free the MTD device structure */
	kfree (tx4938ndfmc_mtd);
}
module_exit(tx4938ndfmc_cleanup);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Alice Hennessy <ahennessy@mvista.com>");
MODULE_DESCRIPTION("Board-specific glue layer for NAND flash on TX4938 NDFMC");
