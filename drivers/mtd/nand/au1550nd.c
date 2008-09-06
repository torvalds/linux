/*
 *  drivers/mtd/nand/au1550nd.c
 *
 *  Copyright (C) 2004 Embedded Edge, LLC
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */

#include <linux/slab.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/interrupt.h>
#include <linux/mtd/mtd.h>
#include <linux/mtd/nand.h>
#include <linux/mtd/partitions.h>
#include <asm/io.h>

#include <asm/mach-au1x00/au1xxx.h>

/*
 * MTD structure for NAND controller
 */
static struct mtd_info *au1550_mtd = NULL;
static void __iomem *p_nand;
static int nand_width = 1;	/* default x8 */
static void (*au1550_write_byte)(struct mtd_info *, u_char);

/*
 * Define partitions for flash device
 */
static const struct mtd_partition partition_info[] = {
	{
	 .name = "NAND FS 0",
	 .offset = 0,
	 .size = 8 * 1024 * 1024},
	{
	 .name = "NAND FS 1",
	 .offset = MTDPART_OFS_APPEND,
	 .size = MTDPART_SIZ_FULL}
};

/**
 * au_read_byte -  read one byte from the chip
 * @mtd:	MTD device structure
 *
 *  read function for 8bit buswith
 */
static u_char au_read_byte(struct mtd_info *mtd)
{
	struct nand_chip *this = mtd->priv;
	u_char ret = readb(this->IO_ADDR_R);
	au_sync();
	return ret;
}

/**
 * au_write_byte -  write one byte to the chip
 * @mtd:	MTD device structure
 * @byte:	pointer to data byte to write
 *
 *  write function for 8it buswith
 */
static void au_write_byte(struct mtd_info *mtd, u_char byte)
{
	struct nand_chip *this = mtd->priv;
	writeb(byte, this->IO_ADDR_W);
	au_sync();
}

/**
 * au_read_byte16 -  read one byte endianess aware from the chip
 * @mtd:	MTD device structure
 *
 *  read function for 16bit buswith with
 * endianess conversion
 */
static u_char au_read_byte16(struct mtd_info *mtd)
{
	struct nand_chip *this = mtd->priv;
	u_char ret = (u_char) cpu_to_le16(readw(this->IO_ADDR_R));
	au_sync();
	return ret;
}

/**
 * au_write_byte16 -  write one byte endianess aware to the chip
 * @mtd:	MTD device structure
 * @byte:	pointer to data byte to write
 *
 *  write function for 16bit buswith with
 * endianess conversion
 */
static void au_write_byte16(struct mtd_info *mtd, u_char byte)
{
	struct nand_chip *this = mtd->priv;
	writew(le16_to_cpu((u16) byte), this->IO_ADDR_W);
	au_sync();
}

/**
 * au_read_word -  read one word from the chip
 * @mtd:	MTD device structure
 *
 *  read function for 16bit buswith without
 * endianess conversion
 */
static u16 au_read_word(struct mtd_info *mtd)
{
	struct nand_chip *this = mtd->priv;
	u16 ret = readw(this->IO_ADDR_R);
	au_sync();
	return ret;
}

/**
 * au_write_buf -  write buffer to chip
 * @mtd:	MTD device structure
 * @buf:	data buffer
 * @len:	number of bytes to write
 *
 *  write function for 8bit buswith
 */
static void au_write_buf(struct mtd_info *mtd, const u_char *buf, int len)
{
	int i;
	struct nand_chip *this = mtd->priv;

	for (i = 0; i < len; i++) {
		writeb(buf[i], this->IO_ADDR_W);
		au_sync();
	}
}

/**
 * au_read_buf -  read chip data into buffer
 * @mtd:	MTD device structure
 * @buf:	buffer to store date
 * @len:	number of bytes to read
 *
 *  read function for 8bit buswith
 */
static void au_read_buf(struct mtd_info *mtd, u_char *buf, int len)
{
	int i;
	struct nand_chip *this = mtd->priv;

	for (i = 0; i < len; i++) {
		buf[i] = readb(this->IO_ADDR_R);
		au_sync();
	}
}

/**
 * au_verify_buf -  Verify chip data against buffer
 * @mtd:	MTD device structure
 * @buf:	buffer containing the data to compare
 * @len:	number of bytes to compare
 *
 *  verify function for 8bit buswith
 */
static int au_verify_buf(struct mtd_info *mtd, const u_char *buf, int len)
{
	int i;
	struct nand_chip *this = mtd->priv;

	for (i = 0; i < len; i++) {
		if (buf[i] != readb(this->IO_ADDR_R))
			return -EFAULT;
		au_sync();
	}

	return 0;
}

/**
 * au_write_buf16 -  write buffer to chip
 * @mtd:	MTD device structure
 * @buf:	data buffer
 * @len:	number of bytes to write
 *
 *  write function for 16bit buswith
 */
static void au_write_buf16(struct mtd_info *mtd, const u_char *buf, int len)
{
	int i;
	struct nand_chip *this = mtd->priv;
	u16 *p = (u16 *) buf;
	len >>= 1;

	for (i = 0; i < len; i++) {
		writew(p[i], this->IO_ADDR_W);
		au_sync();
	}

}

/**
 * au_read_buf16 -  read chip data into buffer
 * @mtd:	MTD device structure
 * @buf:	buffer to store date
 * @len:	number of bytes to read
 *
 *  read function for 16bit buswith
 */
static void au_read_buf16(struct mtd_info *mtd, u_char *buf, int len)
{
	int i;
	struct nand_chip *this = mtd->priv;
	u16 *p = (u16 *) buf;
	len >>= 1;

	for (i = 0; i < len; i++) {
		p[i] = readw(this->IO_ADDR_R);
		au_sync();
	}
}

/**
 * au_verify_buf16 -  Verify chip data against buffer
 * @mtd:	MTD device structure
 * @buf:	buffer containing the data to compare
 * @len:	number of bytes to compare
 *
 *  verify function for 16bit buswith
 */
static int au_verify_buf16(struct mtd_info *mtd, const u_char *buf, int len)
{
	int i;
	struct nand_chip *this = mtd->priv;
	u16 *p = (u16 *) buf;
	len >>= 1;

	for (i = 0; i < len; i++) {
		if (p[i] != readw(this->IO_ADDR_R))
			return -EFAULT;
		au_sync();
	}
	return 0;
}

/* Select the chip by setting nCE to low */
#define NAND_CTL_SETNCE		1
/* Deselect the chip by setting nCE to high */
#define NAND_CTL_CLRNCE		2
/* Select the command latch by setting CLE to high */
#define NAND_CTL_SETCLE		3
/* Deselect the command latch by setting CLE to low */
#define NAND_CTL_CLRCLE		4
/* Select the address latch by setting ALE to high */
#define NAND_CTL_SETALE		5
/* Deselect the address latch by setting ALE to low */
#define NAND_CTL_CLRALE		6

static void au1550_hwcontrol(struct mtd_info *mtd, int cmd)
{
	register struct nand_chip *this = mtd->priv;

	switch (cmd) {

	case NAND_CTL_SETCLE:
		this->IO_ADDR_W = p_nand + MEM_STNAND_CMD;
		break;

	case NAND_CTL_CLRCLE:
		this->IO_ADDR_W = p_nand + MEM_STNAND_DATA;
		break;

	case NAND_CTL_SETALE:
		this->IO_ADDR_W = p_nand + MEM_STNAND_ADDR;
		break;

	case NAND_CTL_CLRALE:
		this->IO_ADDR_W = p_nand + MEM_STNAND_DATA;
		/* FIXME: Nobody knows why this is necessary,
		 * but it works only that way */
		udelay(1);
		break;

	case NAND_CTL_SETNCE:
		/* assert (force assert) chip enable */
		au_writel((1 << (4 + NAND_CS)), MEM_STNDCTL);
		break;

	case NAND_CTL_CLRNCE:
		/* deassert chip enable */
		au_writel(0, MEM_STNDCTL);
		break;
	}

	this->IO_ADDR_R = this->IO_ADDR_W;

	/* Drain the writebuffer */
	au_sync();
}

int au1550_device_ready(struct mtd_info *mtd)
{
	int ret = (au_readl(MEM_STSTAT) & 0x1) ? 1 : 0;
	au_sync();
	return ret;
}

/**
 * au1550_select_chip - control -CE line
 *	Forbid driving -CE manually permitting the NAND controller to do this.
 *	Keeping -CE asserted during the whole sector reads interferes with the
 *	NOR flash and PCMCIA drivers as it causes contention on the static bus.
 *	We only have to hold -CE low for the NAND read commands since the flash
 *	chip needs it to be asserted during chip not ready time but the NAND
 *	controller keeps it released.
 *
 * @mtd:	MTD device structure
 * @chip:	chipnumber to select, -1 for deselect
 */
static void au1550_select_chip(struct mtd_info *mtd, int chip)
{
}

/**
 * au1550_command - Send command to NAND device
 * @mtd:	MTD device structure
 * @command:	the command to be sent
 * @column:	the column address for this command, -1 if none
 * @page_addr:	the page address for this command, -1 if none
 */
static void au1550_command(struct mtd_info *mtd, unsigned command, int column, int page_addr)
{
	register struct nand_chip *this = mtd->priv;
	int ce_override = 0, i;
	ulong flags;

	/* Begin command latch cycle */
	au1550_hwcontrol(mtd, NAND_CTL_SETCLE);
	/*
	 * Write out the command to the device.
	 */
	if (command == NAND_CMD_SEQIN) {
		int readcmd;

		if (column >= mtd->writesize) {
			/* OOB area */
			column -= mtd->writesize;
			readcmd = NAND_CMD_READOOB;
		} else if (column < 256) {
			/* First 256 bytes --> READ0 */
			readcmd = NAND_CMD_READ0;
		} else {
			column -= 256;
			readcmd = NAND_CMD_READ1;
		}
		au1550_write_byte(mtd, readcmd);
	}
	au1550_write_byte(mtd, command);

	/* Set ALE and clear CLE to start address cycle */
	au1550_hwcontrol(mtd, NAND_CTL_CLRCLE);

	if (column != -1 || page_addr != -1) {
		au1550_hwcontrol(mtd, NAND_CTL_SETALE);

		/* Serially input address */
		if (column != -1) {
			/* Adjust columns for 16 bit buswidth */
			if (this->options & NAND_BUSWIDTH_16)
				column >>= 1;
			au1550_write_byte(mtd, column);
		}
		if (page_addr != -1) {
			au1550_write_byte(mtd, (u8)(page_addr & 0xff));

			if (command == NAND_CMD_READ0 ||
			    command == NAND_CMD_READ1 ||
			    command == NAND_CMD_READOOB) {
				/*
				 * NAND controller will release -CE after
				 * the last address byte is written, so we'll
				 * have to forcibly assert it. No interrupts
				 * are allowed while we do this as we don't
				 * want the NOR flash or PCMCIA drivers to
				 * steal our precious bytes of data...
				 */
				ce_override = 1;
				local_irq_save(flags);
				au1550_hwcontrol(mtd, NAND_CTL_SETNCE);
			}

			au1550_write_byte(mtd, (u8)(page_addr >> 8));

			/* One more address cycle for devices > 32MiB */
			if (this->chipsize > (32 << 20))
				au1550_write_byte(mtd, (u8)((page_addr >> 16) & 0x0f));
		}
		/* Latch in address */
		au1550_hwcontrol(mtd, NAND_CTL_CLRALE);
	}

	/*
	 * Program and erase have their own busy handlers.
	 * Status and sequential in need no delay.
	 */
	switch (command) {

	case NAND_CMD_PAGEPROG:
	case NAND_CMD_ERASE1:
	case NAND_CMD_ERASE2:
	case NAND_CMD_SEQIN:
	case NAND_CMD_STATUS:
		return;

	case NAND_CMD_RESET:
		break;

	case NAND_CMD_READ0:
	case NAND_CMD_READ1:
	case NAND_CMD_READOOB:
		/* Check if we're really driving -CE low (just in case) */
		if (unlikely(!ce_override))
			break;

		/* Apply a short delay always to ensure that we do wait tWB. */
		ndelay(100);
		/* Wait for a chip to become ready... */
		for (i = this->chip_delay; !this->dev_ready(mtd) && i > 0; --i)
			udelay(1);

		/* Release -CE and re-enable interrupts. */
		au1550_hwcontrol(mtd, NAND_CTL_CLRNCE);
		local_irq_restore(flags);
		return;
	}
	/* Apply this short delay always to ensure that we do wait tWB. */
	ndelay(100);

	while(!this->dev_ready(mtd));
}


/*
 * Main initialization routine
 */
static int __init au1xxx_nand_init(void)
{
	struct nand_chip *this;
	u16 boot_swapboot = 0;	/* default value */
	int retval;
	u32 mem_staddr;
	u32 nand_phys;

	/* Allocate memory for MTD device structure and private data */
	au1550_mtd = kmalloc(sizeof(struct mtd_info) + sizeof(struct nand_chip), GFP_KERNEL);
	if (!au1550_mtd) {
		printk("Unable to allocate NAND MTD dev structure.\n");
		return -ENOMEM;
	}

	/* Get pointer to private data */
	this = (struct nand_chip *)(&au1550_mtd[1]);

	/* Initialize structures */
	memset(au1550_mtd, 0, sizeof(struct mtd_info));
	memset(this, 0, sizeof(struct nand_chip));

	/* Link the private data with the MTD structure */
	au1550_mtd->priv = this;
	au1550_mtd->owner = THIS_MODULE;


	/* MEM_STNDCTL: disable ints, disable nand boot */
	au_writel(0, MEM_STNDCTL);

#ifdef CONFIG_MIPS_PB1550
	/* set gpio206 high */
	au_writel(au_readl(GPIO2_DIR) & ~(1 << 6), GPIO2_DIR);

	boot_swapboot = (au_readl(MEM_STSTAT) & (0x7 << 1)) | ((bcsr->status >> 6) & 0x1);
	switch (boot_swapboot) {
	case 0:
	case 2:
	case 8:
	case 0xC:
	case 0xD:
		/* x16 NAND Flash */
		nand_width = 0;
		break;
	case 1:
	case 9:
	case 3:
	case 0xE:
	case 0xF:
		/* x8 NAND Flash */
		nand_width = 1;
		break;
	default:
		printk("Pb1550 NAND: bad boot:swap\n");
		retval = -EINVAL;
		goto outmem;
	}
#endif

	/* Configure chip-select; normally done by boot code, e.g. YAMON */
#ifdef NAND_STCFG
	if (NAND_CS == 0) {
		au_writel(NAND_STCFG,  MEM_STCFG0);
		au_writel(NAND_STTIME, MEM_STTIME0);
		au_writel(NAND_STADDR, MEM_STADDR0);
	}
	if (NAND_CS == 1) {
		au_writel(NAND_STCFG,  MEM_STCFG1);
		au_writel(NAND_STTIME, MEM_STTIME1);
		au_writel(NAND_STADDR, MEM_STADDR1);
	}
	if (NAND_CS == 2) {
		au_writel(NAND_STCFG,  MEM_STCFG2);
		au_writel(NAND_STTIME, MEM_STTIME2);
		au_writel(NAND_STADDR, MEM_STADDR2);
	}
	if (NAND_CS == 3) {
		au_writel(NAND_STCFG,  MEM_STCFG3);
		au_writel(NAND_STTIME, MEM_STTIME3);
		au_writel(NAND_STADDR, MEM_STADDR3);
	}
#endif

	/* Locate NAND chip-select in order to determine NAND phys address */
	mem_staddr = 0x00000000;
	if (((au_readl(MEM_STCFG0) & 0x7) == 0x5) && (NAND_CS == 0))
		mem_staddr = au_readl(MEM_STADDR0);
	else if (((au_readl(MEM_STCFG1) & 0x7) == 0x5) && (NAND_CS == 1))
		mem_staddr = au_readl(MEM_STADDR1);
	else if (((au_readl(MEM_STCFG2) & 0x7) == 0x5) && (NAND_CS == 2))
		mem_staddr = au_readl(MEM_STADDR2);
	else if (((au_readl(MEM_STCFG3) & 0x7) == 0x5) && (NAND_CS == 3))
		mem_staddr = au_readl(MEM_STADDR3);

	if (mem_staddr == 0x00000000) {
		printk("Au1xxx NAND: ERROR WITH NAND CHIP-SELECT\n");
		kfree(au1550_mtd);
		return 1;
	}
	nand_phys = (mem_staddr << 4) & 0xFFFC0000;

	p_nand = (void __iomem *)ioremap(nand_phys, 0x1000);

	/* make controller and MTD agree */
	if (NAND_CS == 0)
		nand_width = au_readl(MEM_STCFG0) & (1 << 22);
	if (NAND_CS == 1)
		nand_width = au_readl(MEM_STCFG1) & (1 << 22);
	if (NAND_CS == 2)
		nand_width = au_readl(MEM_STCFG2) & (1 << 22);
	if (NAND_CS == 3)
		nand_width = au_readl(MEM_STCFG3) & (1 << 22);

	/* Set address of hardware control function */
	this->dev_ready = au1550_device_ready;
	this->select_chip = au1550_select_chip;
	this->cmdfunc = au1550_command;

	/* 30 us command delay time */
	this->chip_delay = 30;
	this->ecc.mode = NAND_ECC_SOFT;

	this->options = NAND_NO_AUTOINCR;

	if (!nand_width)
		this->options |= NAND_BUSWIDTH_16;

	this->read_byte = (!nand_width) ? au_read_byte16 : au_read_byte;
	au1550_write_byte = (!nand_width) ? au_write_byte16 : au_write_byte;
	this->read_word = au_read_word;
	this->write_buf = (!nand_width) ? au_write_buf16 : au_write_buf;
	this->read_buf = (!nand_width) ? au_read_buf16 : au_read_buf;
	this->verify_buf = (!nand_width) ? au_verify_buf16 : au_verify_buf;

	/* Scan to find existence of the device */
	if (nand_scan(au1550_mtd, 1)) {
		retval = -ENXIO;
		goto outio;
	}

	/* Register the partitions */
	add_mtd_partitions(au1550_mtd, partition_info, ARRAY_SIZE(partition_info));

	return 0;

 outio:
	iounmap((void *)p_nand);

 outmem:
	kfree(au1550_mtd);
	return retval;
}

module_init(au1xxx_nand_init);

/*
 * Clean up routine
 */
static void __exit au1550_cleanup(void)
{
	/* Release resources, unregister device */
	nand_release(au1550_mtd);

	/* Free the MTD device structure */
	kfree(au1550_mtd);

	/* Unmap */
	iounmap((void *)p_nand);
}

module_exit(au1550_cleanup);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Embedded Edge, LLC");
MODULE_DESCRIPTION("Board-specific glue layer for NAND flash on Pb1550 board");
