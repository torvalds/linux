/*
 *  drivers/mtd/tx4925ndfmc.c
 *
 *  Overview:
 *   This is a device driver for the NAND flash device found on the
 *   Toshiba RBTX4925 reference board, which is a SmartMediaCard. It supports 
 *   16MiB, 32MiB and 64MiB cards.
 *
 * Author: MontaVista Software, Inc.  source@mvista.com
 *
 * Derived from drivers/mtd/autcpu12.c
 *       Copyright (c) 2001 Thomas Gleixner (gleixner@autronix.de)
 *
 * $Id: tx4925ndfmc.c,v 1.5 2004/10/05 13:50:20 gleixner Exp $
 *
 * Copyright (C) 2001 Toshiba Corporation 
 * 
 * 2003 (c) MontaVista Software, Inc. This file is licensed under
 * the terms of the GNU General Public License version 2. This program
 * is licensed "as is" without any warranty of any kind, whether express
 * or implied.
 *
 */

#include <linux/slab.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/mtd/mtd.h>
#include <linux/mtd/nand.h>
#include <linux/mtd/partitions.h>
#include <linux/delay.h>
#include <asm/io.h>
#include <asm/tx4925/tx4925_nand.h>

extern struct nand_oobinfo jffs2_oobinfo;

/*
 * MTD structure for RBTX4925 board
 */
static struct mtd_info *tx4925ndfmc_mtd = NULL;

/*
 * Define partitions for flash devices
 */

static struct mtd_partition partition_info16k[] = {
	{ .name = "RBTX4925 flash partition 1",
	  .offset =  0,
	  .size =    8 * 0x00100000 },
	{ .name = "RBTX4925 flash partition 2",
	  .offset =  8 * 0x00100000,
	  .size =    8 * 0x00100000 },
};

static struct mtd_partition partition_info32k[] = {
	{ .name = "RBTX4925 flash partition 1",
	  .offset =  0,
	  .size =    8 * 0x00100000 },
	{ .name = "RBTX4925 flash partition 2",
	  .offset = 8 * 0x00100000,
	  .size =  24 * 0x00100000 },
};

static struct mtd_partition partition_info64k[] = {
	{ .name = "User FS",
	  .offset =  0,
	  .size =   16 * 0x00100000 },
	{ .name = "RBTX4925 flash partition 2",
	  .offset = 16 * 0x00100000,
	  .size =   48 * 0x00100000},
};

static struct mtd_partition partition_info128k[] = {
	{ .name = "Skip bad section",
	  .offset =  0,
	  .size =   16 * 0x00100000 },
	{ .name = "User FS",
	  .offset = 16 * 0x00100000,
	  .size =   112 * 0x00100000 },
};
#define NUM_PARTITIONS16K  2
#define NUM_PARTITIONS32K  2
#define NUM_PARTITIONS64K  2
#define NUM_PARTITIONS128K 2

/* 
 *	hardware specific access to control-lines
*/
static void tx4925ndfmc_hwcontrol(struct mtd_info *mtd, int cmd)
{

	switch(cmd){

		case NAND_CTL_SETCLE: 
			tx4925_ndfmcptr->mcr |= TX4925_NDFMCR_CLE;
			break;
		case NAND_CTL_CLRCLE:
			tx4925_ndfmcptr->mcr &= ~TX4925_NDFMCR_CLE;
			break;
		case NAND_CTL_SETALE:
			tx4925_ndfmcptr->mcr |= TX4925_NDFMCR_ALE;
			break;
		case NAND_CTL_CLRALE: 
			tx4925_ndfmcptr->mcr &= ~TX4925_NDFMCR_ALE;
			break;
		case NAND_CTL_SETNCE:
			tx4925_ndfmcptr->mcr |= TX4925_NDFMCR_CE;
			break;
		case NAND_CTL_CLRNCE:
			tx4925_ndfmcptr->mcr &= ~TX4925_NDFMCR_CE;
			break;
		case NAND_CTL_SETWP:
			tx4925_ndfmcptr->mcr |= TX4925_NDFMCR_WE;
			break;
		case NAND_CTL_CLRWP:
			tx4925_ndfmcptr->mcr &= ~TX4925_NDFMCR_WE;
			break;
	}
}

/*
*	read device ready pin
*/
static int tx4925ndfmc_device_ready(struct mtd_info *mtd)
{
	int ready;
	ready = (tx4925_ndfmcptr->sr & TX4925_NDSFR_BUSY) ? 0 : 1;
	return ready;
}
void tx4925ndfmc_enable_hwecc(struct mtd_info *mtd, int mode)
{
	/* reset first */
	tx4925_ndfmcptr->mcr |= TX4925_NDFMCR_ECC_CNTL_MASK;
	tx4925_ndfmcptr->mcr &= ~TX4925_NDFMCR_ECC_CNTL_MASK;
	tx4925_ndfmcptr->mcr |= TX4925_NDFMCR_ECC_CNTL_ENAB;
}
static void tx4925ndfmc_disable_ecc(void)
{
	tx4925_ndfmcptr->mcr &= ~TX4925_NDFMCR_ECC_CNTL_MASK;
}
static void tx4925ndfmc_enable_read_ecc(void)
{
	tx4925_ndfmcptr->mcr &= ~TX4925_NDFMCR_ECC_CNTL_MASK;
	tx4925_ndfmcptr->mcr |= TX4925_NDFMCR_ECC_CNTL_READ;
}
void tx4925ndfmc_readecc(struct mtd_info *mtd, const u_char *dat, u_char *ecc_code){
	int i;
	u_char *ecc = ecc_code;
        tx4925ndfmc_enable_read_ecc();
	for (i = 0;i < 6;i++,ecc++)
		*ecc = tx4925_read_nfmc(&(tx4925_ndfmcptr->dtr));
        tx4925ndfmc_disable_ecc();
}
void tx4925ndfmc_device_setup(void)
{

	*(unsigned char *)0xbb005000 &= ~0x08;

        /* reset NDFMC */
        tx4925_ndfmcptr->rstr |= TX4925_NDFRSTR_RST;
	while (tx4925_ndfmcptr->rstr & TX4925_NDFRSTR_RST);       

	/* setup BusSeparete, Hold Time, Strobe Pulse Width */
	tx4925_ndfmcptr->mcr = TX4925_BSPRT ? TX4925_NDFMCR_BSPRT : 0;
	tx4925_ndfmcptr->spr = TX4925_HOLD << 4 | TX4925_SPW;             
}
static u_char tx4925ndfmc_nand_read_byte(struct mtd_info *mtd)
{
        struct nand_chip *this = mtd->priv;
        return tx4925_read_nfmc(this->IO_ADDR_R);
}

static void tx4925ndfmc_nand_write_byte(struct mtd_info *mtd, u_char byte)
{
        struct nand_chip *this = mtd->priv;
        tx4925_write_nfmc(byte, this->IO_ADDR_W);
}

static void tx4925ndfmc_nand_write_buf(struct mtd_info *mtd, const u_char *buf, int len)
{
	int i;
	struct nand_chip *this = mtd->priv;

	for (i=0; i<len; i++)
		tx4925_write_nfmc(buf[i], this->IO_ADDR_W);
}

static void tx4925ndfmc_nand_read_buf(struct mtd_info *mtd, u_char *buf, int len)
{
	int i;
	struct nand_chip *this = mtd->priv;

	for (i=0; i<len; i++)
		buf[i] = tx4925_read_nfmc(this->IO_ADDR_R);
}

static int tx4925ndfmc_nand_verify_buf(struct mtd_info *mtd, const u_char *buf, int len)
{
	int i;
	struct nand_chip *this = mtd->priv;

	for (i=0; i<len; i++)
		if (buf[i] != tx4925_read_nfmc(this->IO_ADDR_R))
			return -EFAULT;

	return 0;
}

/*
 * Send command to NAND device
 */
static void tx4925ndfmc_nand_command (struct mtd_info *mtd, unsigned command, int column, int page_addr)
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
extern int parse_cmdline_partitions(struct mtd_info *master, struct mtd_partitio
n **pparts, char *);
#endif

/*
 * Main initialization routine
 */
extern int nand_correct_data(struct mtd_info *mtd, u_char *dat, u_char *read_ecc, u_char *calc_ecc);
int __init tx4925ndfmc_init (void)
{
	struct nand_chip *this;
	int err = 0;

	/* Allocate memory for MTD device structure and private data */
	tx4925ndfmc_mtd = kmalloc (sizeof(struct mtd_info) + sizeof (struct nand_chip),
				GFP_KERNEL);
	if (!tx4925ndfmc_mtd) {
		printk ("Unable to allocate RBTX4925 NAND MTD device structure.\n");
		err = -ENOMEM;
		goto out;
	}

        tx4925ndfmc_device_setup();

	/* io is indirect via a register so don't need to ioremap address */

	/* Get pointer to private data */
	this = (struct nand_chip *) (&tx4925ndfmc_mtd[1]);

	/* Initialize structures */
	memset((char *) tx4925ndfmc_mtd, 0, sizeof(struct mtd_info));
	memset((char *) this, 0, sizeof(struct nand_chip));

	/* Link the private data with the MTD structure */
	tx4925ndfmc_mtd->priv = this;

	/* Set address of NAND IO lines */
	this->IO_ADDR_R = (void __iomem *)&(tx4925_ndfmcptr->dtr);
	this->IO_ADDR_W = (void __iomem *)&(tx4925_ndfmcptr->dtr);
	this->hwcontrol = tx4925ndfmc_hwcontrol;
	this->enable_hwecc = tx4925ndfmc_enable_hwecc;
	this->calculate_ecc = tx4925ndfmc_readecc;
	this->correct_data = nand_correct_data;
	this->eccmode = NAND_ECC_HW6_512;	
	this->dev_ready = tx4925ndfmc_device_ready;
	/* 20 us command delay time */
	this->chip_delay = 20;		
        this->read_byte = tx4925ndfmc_nand_read_byte;
        this->write_byte = tx4925ndfmc_nand_write_byte;
	this->cmdfunc = tx4925ndfmc_nand_command;
	this->write_buf = tx4925ndfmc_nand_write_buf;
	this->read_buf = tx4925ndfmc_nand_read_buf;
	this->verify_buf = tx4925ndfmc_nand_verify_buf;

	/* Scan to find existance of the device */
	if (nand_scan (tx4925ndfmc_mtd, 1)) {
		err = -ENXIO;
		goto out_ior;
	}

	/* Register the partitions */
#ifdef CONFIG_MTD_CMDLINE_PARTS
        {
                int mtd_parts_nb = 0;
                struct mtd_partition *mtd_parts = 0;
                mtd_parts_nb = parse_cmdline_partitions(tx4925ndfmc_mtd, &mtd_parts, "tx4925ndfmc");
                if (mtd_parts_nb > 0)
                        add_mtd_partitions(tx4925ndfmc_mtd, mtd_parts, mtd_parts_nb);
                else
                        add_mtd_device(tx4925ndfmc_mtd);
        }
#else /* ifdef CONFIG_MTD_CMDLINE_PARTS */
	switch(tx4925ndfmc_mtd->size){
		case 0x01000000: add_mtd_partitions(tx4925ndfmc_mtd, partition_info16k, NUM_PARTITIONS16K); break;
		case 0x02000000: add_mtd_partitions(tx4925ndfmc_mtd, partition_info32k, NUM_PARTITIONS32K); break;
		case 0x04000000: add_mtd_partitions(tx4925ndfmc_mtd, partition_info64k, NUM_PARTITIONS64K); break; 
		case 0x08000000: add_mtd_partitions(tx4925ndfmc_mtd, partition_info128k, NUM_PARTITIONS128K); break; 
		default: {
			printk ("Unsupported SmartMedia device\n"); 
			err = -ENXIO;
			goto out_ior;
		}
	}
#endif /* ifdef CONFIG_MTD_CMDLINE_PARTS */
	goto out;

out_ior:
out:
	return err;
}

module_init(tx4925ndfmc_init);

/*
 * Clean up routine
 */
#ifdef MODULE
static void __exit tx4925ndfmc_cleanup (void)
{
	/* Release resources, unregister device */
	nand_release (tx4925ndfmc_mtd);

	/* Free the MTD device structure */
	kfree (tx4925ndfmc_mtd);
}
module_exit(tx4925ndfmc_cleanup);
#endif

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Alice Hennessy <ahennessy@mvista.com>");
MODULE_DESCRIPTION("Glue layer for SmartMediaCard on Toshiba RBTX4925");
