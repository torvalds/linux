/*
 * drivers/mtd/nand/at91_nand.c
 *
 *  Copyright (C) 2003 Rick Bronson
 *
 *  Derived from drivers/mtd/nand/autcpu12.c
 *	 Copyright (c) 2001 Thomas Gleixner (gleixner@autronix.de)
 *
 *  Derived from drivers/mtd/spia.c
 *	 Copyright (C) 2000 Steven J. Hill (sjhill@cotw.com)
 *
 *
 *  Add Hardware ECC support for AT91SAM9260 / AT91SAM9263
 *     Richard Genoud (richard.genoud@gmail.com), Adeneo Copyright (C) 2007
 *
 *     Derived from Das U-Boot source code
 *     		(u-boot-1.1.5/board/atmel/at91sam9263ek/nand.c)
 *     (C) Copyright 2006 ATMEL Rousset, Lacressonniere Nicolas
 *
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */

#include <linux/slab.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/mtd/mtd.h>
#include <linux/mtd/nand.h>
#include <linux/mtd/partitions.h>

#include <asm/io.h>
#include <asm/sizes.h>

#include <asm/hardware.h>
#include <asm/arch/board.h>
#include <asm/arch/gpio.h>

#ifdef CONFIG_MTD_NAND_AT91_ECC_HW
#define hard_ecc	1
#else
#define hard_ecc	0
#endif

#ifdef CONFIG_MTD_NAND_AT91_ECC_NONE
#define no_ecc		1
#else
#define no_ecc		0
#endif

/* Register access macros */
#define ecc_readl(add, reg)				\
	__raw_readl(add + AT91_ECC_##reg)
#define ecc_writel(add, reg, value)			\
	__raw_writel((value), add + AT91_ECC_##reg)

#include <asm/arch/at91_ecc.h> /* AT91SAM9260/3 ECC registers */

/* oob layout for large page size
 * bad block info is on bytes 0 and 1
 * the bytes have to be consecutives to avoid
 * several NAND_CMD_RNDOUT during read
 */
static struct nand_ecclayout at91_oobinfo_large = {
	.eccbytes = 4,
	.eccpos = {60, 61, 62, 63},
	.oobfree = {
		{2, 58}
	},
};

/* oob layout for small page size
 * bad block info is on bytes 4 and 5
 * the bytes have to be consecutives to avoid
 * several NAND_CMD_RNDOUT during read
 */
static struct nand_ecclayout at91_oobinfo_small = {
	.eccbytes = 4,
	.eccpos = {0, 1, 2, 3},
	.oobfree = {
		{6, 10}
	},
};

struct at91_nand_host {
	struct nand_chip	nand_chip;
	struct mtd_info		mtd;
	void __iomem		*io_base;
	struct at91_nand_data	*board;
	struct device		*dev;
	void __iomem		*ecc;
};

/*
 * Hardware specific access to control-lines
 */
static void at91_nand_cmd_ctrl(struct mtd_info *mtd, int cmd, unsigned int ctrl)
{
	struct nand_chip *nand_chip = mtd->priv;
	struct at91_nand_host *host = nand_chip->priv;

	if (host->board->enable_pin && (ctrl & NAND_CTRL_CHANGE)) {
		if (ctrl & NAND_NCE)
			at91_set_gpio_value(host->board->enable_pin, 0);
		else
			at91_set_gpio_value(host->board->enable_pin, 1);
	}
	if (cmd == NAND_CMD_NONE)
		return;

	if (ctrl & NAND_CLE)
		writeb(cmd, host->io_base + (1 << host->board->cle));
	else
		writeb(cmd, host->io_base + (1 << host->board->ale));
}

/*
 * Read the Device Ready pin.
 */
static int at91_nand_device_ready(struct mtd_info *mtd)
{
	struct nand_chip *nand_chip = mtd->priv;
	struct at91_nand_host *host = nand_chip->priv;

	return at91_get_gpio_value(host->board->rdy_pin);
}

/*
 * Enable NAND.
 */
static void at91_nand_enable(struct at91_nand_host *host)
{
	if (host->board->enable_pin)
		at91_set_gpio_value(host->board->enable_pin, 0);
}

/*
 * Disable NAND.
 */
static void at91_nand_disable(struct at91_nand_host *host)
{
	if (host->board->enable_pin)
		at91_set_gpio_value(host->board->enable_pin, 1);
}

/*
 * write oob for small pages
 */
static int at91_nand_write_oob_512(struct mtd_info *mtd,
		struct nand_chip *chip, int page)
{
	int chunk = chip->ecc.bytes + chip->ecc.prepad + chip->ecc.postpad;
	int eccsize = chip->ecc.size, length = mtd->oobsize;
	int len, pos, status = 0;
	const uint8_t *bufpoi = chip->oob_poi;

	pos = eccsize + chunk;

	chip->cmdfunc(mtd, NAND_CMD_SEQIN, pos, page);
	len = min_t(int, length, chunk);
	chip->write_buf(mtd, bufpoi, len);
	bufpoi += len;
	length -= len;
	if (length > 0)
		chip->write_buf(mtd, bufpoi, length);

	chip->cmdfunc(mtd, NAND_CMD_PAGEPROG, -1, -1);
	status = chip->waitfunc(mtd, chip);

	return status & NAND_STATUS_FAIL ? -EIO : 0;

}

/*
 * read oob for small pages
 */
static int at91_nand_read_oob_512(struct mtd_info *mtd,
		struct nand_chip *chip,	int page, int sndcmd)
{
	if (sndcmd) {
		chip->cmdfunc(mtd, NAND_CMD_READOOB, 0, page);
		sndcmd = 0;
	}
	chip->read_buf(mtd, chip->oob_poi, mtd->oobsize);
	return sndcmd;
}

/*
 * Calculate HW ECC
 *
 * function called after a write
 *
 * mtd:        MTD block structure
 * dat:        raw data (unused)
 * ecc_code:   buffer for ECC
 */
static int at91_nand_calculate(struct mtd_info *mtd,
		const u_char *dat, unsigned char *ecc_code)
{
	struct nand_chip *nand_chip = mtd->priv;
	struct at91_nand_host *host = nand_chip->priv;
	uint32_t *eccpos = nand_chip->ecc.layout->eccpos;
	unsigned int ecc_value;

	/* get the first 2 ECC bytes */
	ecc_value = ecc_readl(host->ecc, PR);

	ecc_code[eccpos[0]] = ecc_value & 0xFF;
	ecc_code[eccpos[1]] = (ecc_value >> 8) & 0xFF;

	/* get the last 2 ECC bytes */
	ecc_value = ecc_readl(host->ecc, NPR) & AT91_ECC_NPARITY;

	ecc_code[eccpos[2]] = ecc_value & 0xFF;
	ecc_code[eccpos[3]] = (ecc_value >> 8) & 0xFF;

	return 0;
}

/*
 * HW ECC read page function
 *
 * mtd:        mtd info structure
 * chip:       nand chip info structure
 * buf:        buffer to store read data
 */
static int at91_nand_read_page(struct mtd_info *mtd,
		struct nand_chip *chip, uint8_t *buf)
{
	int eccsize = chip->ecc.size;
	int eccbytes = chip->ecc.bytes;
	uint32_t *eccpos = chip->ecc.layout->eccpos;
	uint8_t *p = buf;
	uint8_t *oob = chip->oob_poi;
	uint8_t *ecc_pos;
	int stat;

	/* read the page */
	chip->read_buf(mtd, p, eccsize);

	/* move to ECC position if needed */
	if (eccpos[0] != 0) {
		/* This only works on large pages
		 * because the ECC controller waits for
		 * NAND_CMD_RNDOUTSTART after the
		 * NAND_CMD_RNDOUT.
		 * anyway, for small pages, the eccpos[0] == 0
		 */
		chip->cmdfunc(mtd, NAND_CMD_RNDOUT,
				mtd->writesize + eccpos[0], -1);
	}

	/* the ECC controller needs to read the ECC just after the data */
	ecc_pos = oob + eccpos[0];
	chip->read_buf(mtd, ecc_pos, eccbytes);

	/* check if there's an error */
	stat = chip->ecc.correct(mtd, p, oob, NULL);

	if (stat < 0)
		mtd->ecc_stats.failed++;
	else
		mtd->ecc_stats.corrected += stat;

	/* get back to oob start (end of page) */
	chip->cmdfunc(mtd, NAND_CMD_RNDOUT, mtd->writesize, -1);

	/* read the oob */
	chip->read_buf(mtd, oob, mtd->oobsize);

	return 0;
}

/*
 * HW ECC Correction
 *
 * function called after a read
 *
 * mtd:        MTD block structure
 * dat:        raw data read from the chip
 * read_ecc:   ECC from the chip (unused)
 * isnull:     unused
 *
 * Detect and correct a 1 bit error for a page
 */
static int at91_nand_correct(struct mtd_info *mtd, u_char *dat,
		u_char *read_ecc, u_char *isnull)
{
	struct nand_chip *nand_chip = mtd->priv;
	struct at91_nand_host *host = nand_chip->priv;
	unsigned int ecc_status;
	unsigned int ecc_word, ecc_bit;

	/* get the status from the Status Register */
	ecc_status = ecc_readl(host->ecc, SR);

	/* if there's no error */
	if (likely(!(ecc_status & AT91_ECC_RECERR)))
		return 0;

	/* get error bit offset (4 bits) */
	ecc_bit = ecc_readl(host->ecc, PR) & AT91_ECC_BITADDR;
	/* get word address (12 bits) */
	ecc_word = ecc_readl(host->ecc, PR) & AT91_ECC_WORDADDR;
	ecc_word >>= 4;

	/* if there are multiple errors */
	if (ecc_status & AT91_ECC_MULERR) {
		/* check if it is a freshly erased block
		 * (filled with 0xff) */
		if ((ecc_bit == AT91_ECC_BITADDR)
				&& (ecc_word == (AT91_ECC_WORDADDR >> 4))) {
			/* the block has just been erased, return OK */
			return 0;
		}
		/* it doesn't seems to be a freshly
		 * erased block.
		 * We can't correct so many errors */
		dev_dbg(host->dev, "at91_nand : multiple errors detected."
				" Unable to correct.\n");
		return -EIO;
	}

	/* if there's a single bit error : we can correct it */
	if (ecc_status & AT91_ECC_ECCERR) {
		/* there's nothing much to do here.
		 * the bit error is on the ECC itself.
		 */
		dev_dbg(host->dev, "at91_nand : one bit error on ECC code."
				" Nothing to correct\n");
		return 0;
	}

	dev_dbg(host->dev, "at91_nand : one bit error on data."
			" (word offset in the page :"
			" 0x%x bit offset : 0x%x)\n",
			ecc_word, ecc_bit);
	/* correct the error */
	if (nand_chip->options & NAND_BUSWIDTH_16) {
		/* 16 bits words */
		((unsigned short *) dat)[ecc_word] ^= (1 << ecc_bit);
	} else {
		/* 8 bits words */
		dat[ecc_word] ^= (1 << ecc_bit);
	}
	dev_dbg(host->dev, "at91_nand : error corrected\n");
	return 1;
}

/*
 * Enable HW ECC : unsused
 */
static void at91_nand_hwctl(struct mtd_info *mtd, int mode) { ; }

#ifdef CONFIG_MTD_PARTITIONS
static const char *part_probes[] = { "cmdlinepart", NULL };
#endif

/*
 * Probe for the NAND device.
 */
static int __init at91_nand_probe(struct platform_device *pdev)
{
	struct at91_nand_host *host;
	struct mtd_info *mtd;
	struct nand_chip *nand_chip;
	struct resource *regs;
	struct resource *mem;
	int res;

#ifdef CONFIG_MTD_PARTITIONS
	struct mtd_partition *partitions = NULL;
	int num_partitions = 0;
#endif

	/* Allocate memory for the device structure (and zero it) */
	host = kzalloc(sizeof(struct at91_nand_host), GFP_KERNEL);
	if (!host) {
		printk(KERN_ERR "at91_nand: failed to allocate device structure.\n");
		return -ENOMEM;
	}

	mem = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!mem) {
		printk(KERN_ERR "at91_nand: can't get I/O resource mem\n");
		return -ENXIO;
	}

	host->io_base = ioremap(mem->start, mem->end - mem->start + 1);
	if (host->io_base == NULL) {
		printk(KERN_ERR "at91_nand: ioremap failed\n");
		kfree(host);
		return -EIO;
	}

	mtd = &host->mtd;
	nand_chip = &host->nand_chip;
	host->board = pdev->dev.platform_data;
	host->dev = &pdev->dev;

	nand_chip->priv = host;		/* link the private data structures */
	mtd->priv = nand_chip;
	mtd->owner = THIS_MODULE;

	/* Set address of NAND IO lines */
	nand_chip->IO_ADDR_R = host->io_base;
	nand_chip->IO_ADDR_W = host->io_base;
	nand_chip->cmd_ctrl = at91_nand_cmd_ctrl;

	if (host->board->rdy_pin)
		nand_chip->dev_ready = at91_nand_device_ready;

	regs = platform_get_resource(pdev, IORESOURCE_MEM, 1);
	if (!regs && hard_ecc) {
		printk(KERN_ERR "at91_nand: can't get I/O resource "
				"regs\nFalling back on software ECC\n");
	}

	nand_chip->ecc.mode = NAND_ECC_SOFT;	/* enable ECC */
	if (no_ecc)
		nand_chip->ecc.mode = NAND_ECC_NONE;
	if (hard_ecc && regs) {
		host->ecc = ioremap(regs->start, regs->end - regs->start + 1);
		if (host->ecc == NULL) {
			printk(KERN_ERR "at91_nand: ioremap failed\n");
			res = -EIO;
			goto err_ecc_ioremap;
		}
		nand_chip->ecc.mode = NAND_ECC_HW_SYNDROME;
		nand_chip->ecc.calculate = at91_nand_calculate;
		nand_chip->ecc.correct = at91_nand_correct;
		nand_chip->ecc.hwctl = at91_nand_hwctl;
		nand_chip->ecc.read_page = at91_nand_read_page;
		nand_chip->ecc.bytes = 4;
		nand_chip->ecc.prepad = 0;
		nand_chip->ecc.postpad = 0;
	}

	nand_chip->chip_delay = 20;		/* 20us command delay time */

	if (host->board->bus_width_16)		/* 16-bit bus width */
		nand_chip->options |= NAND_BUSWIDTH_16;

	platform_set_drvdata(pdev, host);
	at91_nand_enable(host);

	if (host->board->det_pin) {
		if (at91_get_gpio_value(host->board->det_pin)) {
			printk ("No SmartMedia card inserted.\n");
			res = ENXIO;
			goto out;
		}
	}

	/* first scan to find the device and get the page size */
	if (nand_scan_ident(mtd, 1)) {
		res = -ENXIO;
		goto out;
	}

	if (nand_chip->ecc.mode == NAND_ECC_HW_SYNDROME) {
		/* ECC is calculated for the whole page (1 step) */
		nand_chip->ecc.size = mtd->writesize;

		/* set ECC page size and oob layout */
		switch (mtd->writesize) {
		case 512:
			nand_chip->ecc.layout = &at91_oobinfo_small;
			nand_chip->ecc.read_oob = at91_nand_read_oob_512;
			nand_chip->ecc.write_oob = at91_nand_write_oob_512;
			ecc_writel(host->ecc, MR, AT91_ECC_PAGESIZE_528);
			break;
		case 1024:
			nand_chip->ecc.layout = &at91_oobinfo_large;
			ecc_writel(host->ecc, MR, AT91_ECC_PAGESIZE_1056);
			break;
		case 2048:
			nand_chip->ecc.layout = &at91_oobinfo_large;
			ecc_writel(host->ecc, MR, AT91_ECC_PAGESIZE_2112);
			break;
		case 4096:
			nand_chip->ecc.layout = &at91_oobinfo_large;
			ecc_writel(host->ecc, MR, AT91_ECC_PAGESIZE_4224);
			break;
		default:
			/* page size not handled by HW ECC */
			/* switching back to soft ECC */
			nand_chip->ecc.mode = NAND_ECC_SOFT;
			nand_chip->ecc.calculate = NULL;
			nand_chip->ecc.correct = NULL;
			nand_chip->ecc.hwctl = NULL;
			nand_chip->ecc.read_page = NULL;
			nand_chip->ecc.postpad = 0;
			nand_chip->ecc.prepad = 0;
			nand_chip->ecc.bytes = 0;
			break;
		}
	}

	/* second phase scan */
	if (nand_scan_tail(mtd)) {
		res = -ENXIO;
		goto out;
	}

#ifdef CONFIG_MTD_PARTITIONS
#ifdef CONFIG_MTD_CMDLINE_PARTS
	mtd->name = "at91_nand";
	num_partitions = parse_mtd_partitions(mtd, part_probes,
					      &partitions, 0);
#endif
	if (num_partitions <= 0 && host->board->partition_info)
		partitions = host->board->partition_info(mtd->size,
							 &num_partitions);

	if ((!partitions) || (num_partitions == 0)) {
		printk(KERN_ERR "at91_nand: No parititions defined, or unsupported device.\n");
		res = ENXIO;
		goto release;
	}

	res = add_mtd_partitions(mtd, partitions, num_partitions);
#else
	res = add_mtd_device(mtd);
#endif

	if (!res)
		return res;

#ifdef CONFIG_MTD_PARTITIONS
release:
#endif
	nand_release(mtd);

out:
	iounmap(host->ecc);

err_ecc_ioremap:
	at91_nand_disable(host);
	platform_set_drvdata(pdev, NULL);
	iounmap(host->io_base);
	kfree(host);
	return res;
}

/*
 * Remove a NAND device.
 */
static int __devexit at91_nand_remove(struct platform_device *pdev)
{
	struct at91_nand_host *host = platform_get_drvdata(pdev);
	struct mtd_info *mtd = &host->mtd;

	nand_release(mtd);

	at91_nand_disable(host);

	iounmap(host->io_base);
	iounmap(host->ecc);
	kfree(host);

	return 0;
}

static struct platform_driver at91_nand_driver = {
	.probe		= at91_nand_probe,
	.remove		= at91_nand_remove,
	.driver		= {
		.name	= "at91_nand",
		.owner	= THIS_MODULE,
	},
};

static int __init at91_nand_init(void)
{
	return platform_driver_register(&at91_nand_driver);
}


static void __exit at91_nand_exit(void)
{
	platform_driver_unregister(&at91_nand_driver);
}


module_init(at91_nand_init);
module_exit(at91_nand_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Rick Bronson");
MODULE_DESCRIPTION("NAND/SmartMedia driver for AT91RM9200 / AT91SAM9");
MODULE_ALIAS("platform:at91_nand");
