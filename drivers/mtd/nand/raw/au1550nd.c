// SPDX-License-Identifier: GPL-2.0-only
/*
 *  Copyright (C) 2004 Embedded Edge, LLC
 */

#include <linux/slab.h>
#include <linux/module.h>
#include <linux/interrupt.h>
#include <linux/mtd/mtd.h>
#include <linux/mtd/rawnand.h>
#include <linux/mtd/partitions.h>
#include <linux/platform_device.h>
#include <asm/io.h>
#include <asm/mach-au1x00/au1000.h>
#include <asm/mach-au1x00/au1550nd.h>


struct au1550nd_ctx {
	struct nand_chip chip;

	int cs;
	void __iomem *base;
	void (*write_byte)(struct nand_chip *, u_char);
};

/**
 * au_read_byte -  read one byte from the chip
 * @this:	NAND chip object
 *
 * read function for 8bit buswidth
 */
static u_char au_read_byte(struct nand_chip *this)
{
	u_char ret = readb(this->legacy.IO_ADDR_R);
	wmb(); /* drain writebuffer */
	return ret;
}

/**
 * au_write_byte -  write one byte to the chip
 * @this:	NAND chip object
 * @byte:	pointer to data byte to write
 *
 * write function for 8it buswidth
 */
static void au_write_byte(struct nand_chip *this, u_char byte)
{
	writeb(byte, this->legacy.IO_ADDR_W);
	wmb(); /* drain writebuffer */
}

/**
 * au_read_byte16 -  read one byte endianness aware from the chip
 * @this:	NAND chip object
 *
 * read function for 16bit buswidth with endianness conversion
 */
static u_char au_read_byte16(struct nand_chip *this)
{
	u_char ret = (u_char) cpu_to_le16(readw(this->legacy.IO_ADDR_R));
	wmb(); /* drain writebuffer */
	return ret;
}

/**
 * au_write_byte16 -  write one byte endianness aware to the chip
 * @this:	NAND chip object
 * @byte:	pointer to data byte to write
 *
 * write function for 16bit buswidth with endianness conversion
 */
static void au_write_byte16(struct nand_chip *this, u_char byte)
{
	writew(le16_to_cpu((u16) byte), this->legacy.IO_ADDR_W);
	wmb(); /* drain writebuffer */
}

/**
 * au_write_buf -  write buffer to chip
 * @this:	NAND chip object
 * @buf:	data buffer
 * @len:	number of bytes to write
 *
 * write function for 8bit buswidth
 */
static void au_write_buf(struct nand_chip *this, const u_char *buf, int len)
{
	int i;

	for (i = 0; i < len; i++) {
		writeb(buf[i], this->legacy.IO_ADDR_W);
		wmb(); /* drain writebuffer */
	}
}

/**
 * au_read_buf -  read chip data into buffer
 * @this:	NAND chip object
 * @buf:	buffer to store date
 * @len:	number of bytes to read
 *
 * read function for 8bit buswidth
 */
static void au_read_buf(struct nand_chip *this, u_char *buf, int len)
{
	int i;

	for (i = 0; i < len; i++) {
		buf[i] = readb(this->legacy.IO_ADDR_R);
		wmb(); /* drain writebuffer */
	}
}

/**
 * au_write_buf16 -  write buffer to chip
 * @this:	NAND chip object
 * @buf:	data buffer
 * @len:	number of bytes to write
 *
 * write function for 16bit buswidth
 */
static void au_write_buf16(struct nand_chip *this, const u_char *buf, int len)
{
	int i;
	u16 *p = (u16 *) buf;
	len >>= 1;

	for (i = 0; i < len; i++) {
		writew(p[i], this->legacy.IO_ADDR_W);
		wmb(); /* drain writebuffer */
	}

}

/**
 * au_read_buf16 -  read chip data into buffer
 * @this:	NAND chip object
 * @buf:	buffer to store date
 * @len:	number of bytes to read
 *
 * read function for 16bit buswidth
 */
static void au_read_buf16(struct nand_chip *this, u_char *buf, int len)
{
	int i;
	u16 *p = (u16 *) buf;
	len >>= 1;

	for (i = 0; i < len; i++) {
		p[i] = readw(this->legacy.IO_ADDR_R);
		wmb(); /* drain writebuffer */
	}
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
	struct nand_chip *this = mtd_to_nand(mtd);
	struct au1550nd_ctx *ctx = container_of(this, struct au1550nd_ctx,
						chip);

	switch (cmd) {

	case NAND_CTL_SETCLE:
		this->legacy.IO_ADDR_W = ctx->base + MEM_STNAND_CMD;
		break;

	case NAND_CTL_CLRCLE:
		this->legacy.IO_ADDR_W = ctx->base + MEM_STNAND_DATA;
		break;

	case NAND_CTL_SETALE:
		this->legacy.IO_ADDR_W = ctx->base + MEM_STNAND_ADDR;
		break;

	case NAND_CTL_CLRALE:
		this->legacy.IO_ADDR_W = ctx->base + MEM_STNAND_DATA;
		/* FIXME: Nobody knows why this is necessary,
		 * but it works only that way */
		udelay(1);
		break;

	case NAND_CTL_SETNCE:
		/* assert (force assert) chip enable */
		alchemy_wrsmem((1 << (4 + ctx->cs)), AU1000_MEM_STNDCTL);
		break;

	case NAND_CTL_CLRNCE:
		/* deassert chip enable */
		alchemy_wrsmem(0, AU1000_MEM_STNDCTL);
		break;
	}

	this->legacy.IO_ADDR_R = this->legacy.IO_ADDR_W;

	wmb(); /* Drain the writebuffer */
}

int au1550_device_ready(struct nand_chip *this)
{
	return (alchemy_rdsmem(AU1000_MEM_STSTAT) & 0x1) ? 1 : 0;
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
 * @this:	NAND chip object
 * @chip:	chipnumber to select, -1 for deselect
 */
static void au1550_select_chip(struct nand_chip *this, int chip)
{
}

/**
 * au1550_command - Send command to NAND device
 * @this:	NAND chip object
 * @command:	the command to be sent
 * @column:	the column address for this command, -1 if none
 * @page_addr:	the page address for this command, -1 if none
 */
static void au1550_command(struct nand_chip *this, unsigned command,
			   int column, int page_addr)
{
	struct mtd_info *mtd = nand_to_mtd(this);
	struct au1550nd_ctx *ctx = container_of(this, struct au1550nd_ctx,
						chip);
	int ce_override = 0, i;
	unsigned long flags = 0;

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
		ctx->write_byte(this, readcmd);
	}
	ctx->write_byte(this, command);

	/* Set ALE and clear CLE to start address cycle */
	au1550_hwcontrol(mtd, NAND_CTL_CLRCLE);

	if (column != -1 || page_addr != -1) {
		au1550_hwcontrol(mtd, NAND_CTL_SETALE);

		/* Serially input address */
		if (column != -1) {
			/* Adjust columns for 16 bit buswidth */
			if (this->options & NAND_BUSWIDTH_16 &&
					!nand_opcode_8bits(command))
				column >>= 1;
			ctx->write_byte(this, column);
		}
		if (page_addr != -1) {
			ctx->write_byte(this, (u8)(page_addr & 0xff));

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

			ctx->write_byte(this, (u8)(page_addr >> 8));

			if (this->options & NAND_ROW_ADDR_3)
				ctx->write_byte(this,
						((page_addr >> 16) & 0x0f));
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
		for (i = this->legacy.chip_delay;
		     !this->legacy.dev_ready(this) && i > 0; --i)
			udelay(1);

		/* Release -CE and re-enable interrupts. */
		au1550_hwcontrol(mtd, NAND_CTL_CLRNCE);
		local_irq_restore(flags);
		return;
	}
	/* Apply this short delay always to ensure that we do wait tWB. */
	ndelay(100);

	while(!this->legacy.dev_ready(this));
}

static int find_nand_cs(unsigned long nand_base)
{
	void __iomem *base =
			(void __iomem *)KSEG1ADDR(AU1000_STATIC_MEM_PHYS_ADDR);
	unsigned long addr, staddr, start, mask, end;
	int i;

	for (i = 0; i < 4; i++) {
		addr = 0x1000 + (i * 0x10);			/* CSx */
		staddr = __raw_readl(base + addr + 0x08);	/* STADDRx */
		/* figure out the decoded range of this CS */
		start = (staddr << 4) & 0xfffc0000;
		mask = (staddr << 18) & 0xfffc0000;
		end = (start | (start - 1)) & ~(start ^ mask);
		if ((nand_base >= start) && (nand_base < end))
			return i;
	}

	return -ENODEV;
}

static int au1550nd_probe(struct platform_device *pdev)
{
	struct au1550nd_platdata *pd;
	struct au1550nd_ctx *ctx;
	struct nand_chip *this;
	struct mtd_info *mtd;
	struct resource *r;
	int ret, cs;

	pd = dev_get_platdata(&pdev->dev);
	if (!pd) {
		dev_err(&pdev->dev, "missing platform data\n");
		return -ENODEV;
	}

	ctx = kzalloc(sizeof(*ctx), GFP_KERNEL);
	if (!ctx)
		return -ENOMEM;

	r = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!r) {
		dev_err(&pdev->dev, "no NAND memory resource\n");
		ret = -ENODEV;
		goto out1;
	}
	if (request_mem_region(r->start, resource_size(r), "au1550-nand")) {
		dev_err(&pdev->dev, "cannot claim NAND memory area\n");
		ret = -ENOMEM;
		goto out1;
	}

	ctx->base = ioremap_nocache(r->start, 0x1000);
	if (!ctx->base) {
		dev_err(&pdev->dev, "cannot remap NAND memory area\n");
		ret = -ENODEV;
		goto out2;
	}

	this = &ctx->chip;
	mtd = nand_to_mtd(this);
	mtd->dev.parent = &pdev->dev;

	/* figure out which CS# r->start belongs to */
	cs = find_nand_cs(r->start);
	if (cs < 0) {
		dev_err(&pdev->dev, "cannot detect NAND chipselect\n");
		ret = -ENODEV;
		goto out3;
	}
	ctx->cs = cs;

	this->legacy.dev_ready = au1550_device_ready;
	this->legacy.select_chip = au1550_select_chip;
	this->legacy.cmdfunc = au1550_command;

	/* 30 us command delay time */
	this->legacy.chip_delay = 30;
	this->ecc.mode = NAND_ECC_SOFT;
	this->ecc.algo = NAND_ECC_HAMMING;

	if (pd->devwidth)
		this->options |= NAND_BUSWIDTH_16;

	this->legacy.read_byte = (pd->devwidth) ? au_read_byte16 : au_read_byte;
	ctx->write_byte = (pd->devwidth) ? au_write_byte16 : au_write_byte;
	this->legacy.write_buf = (pd->devwidth) ? au_write_buf16 : au_write_buf;
	this->legacy.read_buf = (pd->devwidth) ? au_read_buf16 : au_read_buf;

	ret = nand_scan(this, 1);
	if (ret) {
		dev_err(&pdev->dev, "NAND scan failed with %d\n", ret);
		goto out3;
	}

	mtd_device_register(mtd, pd->parts, pd->num_parts);

	platform_set_drvdata(pdev, ctx);

	return 0;

out3:
	iounmap(ctx->base);
out2:
	release_mem_region(r->start, resource_size(r));
out1:
	kfree(ctx);
	return ret;
}

static int au1550nd_remove(struct platform_device *pdev)
{
	struct au1550nd_ctx *ctx = platform_get_drvdata(pdev);
	struct resource *r = platform_get_resource(pdev, IORESOURCE_MEM, 0);

	nand_release(&ctx->chip);
	iounmap(ctx->base);
	release_mem_region(r->start, 0x1000);
	kfree(ctx);
	return 0;
}

static struct platform_driver au1550nd_driver = {
	.driver = {
		.name	= "au1550-nand",
	},
	.probe		= au1550nd_probe,
	.remove		= au1550nd_remove,
};

module_platform_driver(au1550nd_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Embedded Edge, LLC");
MODULE_DESCRIPTION("Board-specific glue layer for NAND flash on Pb1550 board");
