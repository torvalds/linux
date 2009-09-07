/*
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
#include <linux/moduleparam.h>
#include <linux/platform_device.h>
#include <linux/mtd/mtd.h>
#include <linux/mtd/nand.h>
#include <linux/mtd/partitions.h>

#include <linux/gpio.h>
#include <linux/io.h>

#include <mach/board.h>
#include <mach/cpu.h>

#ifdef CONFIG_MTD_NAND_ATMEL_ECC_HW
#define hard_ecc	1
#else
#define hard_ecc	0
#endif

#ifdef CONFIG_MTD_NAND_ATMEL_ECC_NONE
#define no_ecc		1
#else
#define no_ecc		0
#endif

static int on_flash_bbt = 0;
module_param(on_flash_bbt, int, 0);

/* Register access macros */
#define ecc_readl(add, reg)				\
	__raw_readl(add + ATMEL_ECC_##reg)
#define ecc_writel(add, reg, value)			\
	__raw_writel((value), add + ATMEL_ECC_##reg)

#include "atmel_nand_ecc.h"	/* Hardware ECC registers */

/* oob layout for large page size
 * bad block info is on bytes 0 and 1
 * the bytes have to be consecutives to avoid
 * several NAND_CMD_RNDOUT during read
 */
static struct nand_ecclayout atmel_oobinfo_large = {
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
static struct nand_ecclayout atmel_oobinfo_small = {
	.eccbytes = 4,
	.eccpos = {0, 1, 2, 3},
	.oobfree = {
		{6, 10}
	},
};

struct atmel_nand_host {
	struct nand_chip	nand_chip;
	struct mtd_info		mtd;
	void __iomem		*io_base;
	struct atmel_nand_data	*board;
	struct device		*dev;
	void __iomem		*ecc;
};

/*
 * Enable NAND.
 */
static void atmel_nand_enable(struct atmel_nand_host *host)
{
	if (host->board->enable_pin)
		gpio_set_value(host->board->enable_pin, 0);
}

/*
 * Disable NAND.
 */
static void atmel_nand_disable(struct atmel_nand_host *host)
{
	if (host->board->enable_pin)
		gpio_set_value(host->board->enable_pin, 1);
}

/*
 * Hardware specific access to control-lines
 */
static void atmel_nand_cmd_ctrl(struct mtd_info *mtd, int cmd, unsigned int ctrl)
{
	struct nand_chip *nand_chip = mtd->priv;
	struct atmel_nand_host *host = nand_chip->priv;

	if (ctrl & NAND_CTRL_CHANGE) {
		if (ctrl & NAND_NCE)
			atmel_nand_enable(host);
		else
			atmel_nand_disable(host);
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
static int atmel_nand_device_ready(struct mtd_info *mtd)
{
	struct nand_chip *nand_chip = mtd->priv;
	struct atmel_nand_host *host = nand_chip->priv;

	return gpio_get_value(host->board->rdy_pin) ^
                !!host->board->rdy_pin_active_low;
}

/*
 * Minimal-overhead PIO for data access.
 */
static void atmel_read_buf(struct mtd_info *mtd, u8 *buf, int len)
{
	struct nand_chip	*nand_chip = mtd->priv;

	__raw_readsb(nand_chip->IO_ADDR_R, buf, len);
}

static void atmel_read_buf16(struct mtd_info *mtd, u8 *buf, int len)
{
	struct nand_chip	*nand_chip = mtd->priv;

	__raw_readsw(nand_chip->IO_ADDR_R, buf, len / 2);
}

static void atmel_write_buf(struct mtd_info *mtd, const u8 *buf, int len)
{
	struct nand_chip	*nand_chip = mtd->priv;

	__raw_writesb(nand_chip->IO_ADDR_W, buf, len);
}

static void atmel_write_buf16(struct mtd_info *mtd, const u8 *buf, int len)
{
	struct nand_chip	*nand_chip = mtd->priv;

	__raw_writesw(nand_chip->IO_ADDR_W, buf, len / 2);
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
static int atmel_nand_calculate(struct mtd_info *mtd,
		const u_char *dat, unsigned char *ecc_code)
{
	struct nand_chip *nand_chip = mtd->priv;
	struct atmel_nand_host *host = nand_chip->priv;
	uint32_t *eccpos = nand_chip->ecc.layout->eccpos;
	unsigned int ecc_value;

	/* get the first 2 ECC bytes */
	ecc_value = ecc_readl(host->ecc, PR);

	ecc_code[0] = ecc_value & 0xFF;
	ecc_code[1] = (ecc_value >> 8) & 0xFF;

	/* get the last 2 ECC bytes */
	ecc_value = ecc_readl(host->ecc, NPR) & ATMEL_ECC_NPARITY;

	ecc_code[2] = ecc_value & 0xFF;
	ecc_code[3] = (ecc_value >> 8) & 0xFF;

	return 0;
}

/*
 * HW ECC read page function
 *
 * mtd:        mtd info structure
 * chip:       nand chip info structure
 * buf:        buffer to store read data
 */
static int atmel_nand_read_page(struct mtd_info *mtd,
		struct nand_chip *chip, uint8_t *buf)
{
	int eccsize = chip->ecc.size;
	int eccbytes = chip->ecc.bytes;
	uint32_t *eccpos = chip->ecc.layout->eccpos;
	uint8_t *p = buf;
	uint8_t *oob = chip->oob_poi;
	uint8_t *ecc_pos;
	int stat;

	/*
	 * Errata: ALE is incorrectly wired up to the ECC controller
	 * on the AP7000, so it will include the address cycles in the
	 * ECC calculation.
	 *
	 * Workaround: Reset the parity registers before reading the
	 * actual data.
	 */
	if (cpu_is_at32ap7000()) {
		struct atmel_nand_host *host = chip->priv;
		ecc_writel(host->ecc, CR, ATMEL_ECC_RST);
	}

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
static int atmel_nand_correct(struct mtd_info *mtd, u_char *dat,
		u_char *read_ecc, u_char *isnull)
{
	struct nand_chip *nand_chip = mtd->priv;
	struct atmel_nand_host *host = nand_chip->priv;
	unsigned int ecc_status;
	unsigned int ecc_word, ecc_bit;

	/* get the status from the Status Register */
	ecc_status = ecc_readl(host->ecc, SR);

	/* if there's no error */
	if (likely(!(ecc_status & ATMEL_ECC_RECERR)))
		return 0;

	/* get error bit offset (4 bits) */
	ecc_bit = ecc_readl(host->ecc, PR) & ATMEL_ECC_BITADDR;
	/* get word address (12 bits) */
	ecc_word = ecc_readl(host->ecc, PR) & ATMEL_ECC_WORDADDR;
	ecc_word >>= 4;

	/* if there are multiple errors */
	if (ecc_status & ATMEL_ECC_MULERR) {
		/* check if it is a freshly erased block
		 * (filled with 0xff) */
		if ((ecc_bit == ATMEL_ECC_BITADDR)
				&& (ecc_word == (ATMEL_ECC_WORDADDR >> 4))) {
			/* the block has just been erased, return OK */
			return 0;
		}
		/* it doesn't seems to be a freshly
		 * erased block.
		 * We can't correct so many errors */
		dev_dbg(host->dev, "atmel_nand : multiple errors detected."
				" Unable to correct.\n");
		return -EIO;
	}

	/* if there's a single bit error : we can correct it */
	if (ecc_status & ATMEL_ECC_ECCERR) {
		/* there's nothing much to do here.
		 * the bit error is on the ECC itself.
		 */
		dev_dbg(host->dev, "atmel_nand : one bit error on ECC code."
				" Nothing to correct\n");
		return 0;
	}

	dev_dbg(host->dev, "atmel_nand : one bit error on data."
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
	dev_dbg(host->dev, "atmel_nand : error corrected\n");
	return 1;
}

/*
 * Enable HW ECC : unused on most chips
 */
static void atmel_nand_hwctl(struct mtd_info *mtd, int mode)
{
	if (cpu_is_at32ap7000()) {
		struct nand_chip *nand_chip = mtd->priv;
		struct atmel_nand_host *host = nand_chip->priv;
		ecc_writel(host->ecc, CR, ATMEL_ECC_RST);
	}
}

#ifdef CONFIG_MTD_PARTITIONS
static const char *part_probes[] = { "cmdlinepart", NULL };
#endif

/*
 * Probe for the NAND device.
 */
static int __init atmel_nand_probe(struct platform_device *pdev)
{
	struct atmel_nand_host *host;
	struct mtd_info *mtd;
	struct nand_chip *nand_chip;
	struct resource *regs;
	struct resource *mem;
	int res;

#ifdef CONFIG_MTD_PARTITIONS
	struct mtd_partition *partitions = NULL;
	int num_partitions = 0;
#endif

	mem = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!mem) {
		printk(KERN_ERR "atmel_nand: can't get I/O resource mem\n");
		return -ENXIO;
	}

	/* Allocate memory for the device structure (and zero it) */
	host = kzalloc(sizeof(struct atmel_nand_host), GFP_KERNEL);
	if (!host) {
		printk(KERN_ERR "atmel_nand: failed to allocate device structure.\n");
		return -ENOMEM;
	}

	host->io_base = ioremap(mem->start, mem->end - mem->start + 1);
	if (host->io_base == NULL) {
		printk(KERN_ERR "atmel_nand: ioremap failed\n");
		res = -EIO;
		goto err_nand_ioremap;
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
	nand_chip->cmd_ctrl = atmel_nand_cmd_ctrl;

	if (host->board->rdy_pin)
		nand_chip->dev_ready = atmel_nand_device_ready;

	regs = platform_get_resource(pdev, IORESOURCE_MEM, 1);
	if (!regs && hard_ecc) {
		printk(KERN_ERR "atmel_nand: can't get I/O resource "
				"regs\nFalling back on software ECC\n");
	}

	nand_chip->ecc.mode = NAND_ECC_SOFT;	/* enable ECC */
	if (no_ecc)
		nand_chip->ecc.mode = NAND_ECC_NONE;
	if (hard_ecc && regs) {
		host->ecc = ioremap(regs->start, regs->end - regs->start + 1);
		if (host->ecc == NULL) {
			printk(KERN_ERR "atmel_nand: ioremap failed\n");
			res = -EIO;
			goto err_ecc_ioremap;
		}
		nand_chip->ecc.mode = NAND_ECC_HW;
		nand_chip->ecc.calculate = atmel_nand_calculate;
		nand_chip->ecc.correct = atmel_nand_correct;
		nand_chip->ecc.hwctl = atmel_nand_hwctl;
		nand_chip->ecc.read_page = atmel_nand_read_page;
		nand_chip->ecc.bytes = 4;
	}

	nand_chip->chip_delay = 20;		/* 20us command delay time */

	if (host->board->bus_width_16) {	/* 16-bit bus width */
		nand_chip->options |= NAND_BUSWIDTH_16;
		nand_chip->read_buf = atmel_read_buf16;
		nand_chip->write_buf = atmel_write_buf16;
	} else {
		nand_chip->read_buf = atmel_read_buf;
		nand_chip->write_buf = atmel_write_buf;
	}

	platform_set_drvdata(pdev, host);
	atmel_nand_enable(host);

	if (host->board->det_pin) {
		if (gpio_get_value(host->board->det_pin)) {
			printk(KERN_INFO "No SmartMedia card inserted.\n");
			res = ENXIO;
			goto err_no_card;
		}
	}

	if (on_flash_bbt) {
		printk(KERN_INFO "atmel_nand: Use On Flash BBT\n");
		nand_chip->options |= NAND_USE_FLASH_BBT;
	}

	/* first scan to find the device and get the page size */
	if (nand_scan_ident(mtd, 1)) {
		res = -ENXIO;
		goto err_scan_ident;
	}

	if (nand_chip->ecc.mode == NAND_ECC_HW) {
		/* ECC is calculated for the whole page (1 step) */
		nand_chip->ecc.size = mtd->writesize;

		/* set ECC page size and oob layout */
		switch (mtd->writesize) {
		case 512:
			nand_chip->ecc.layout = &atmel_oobinfo_small;
			ecc_writel(host->ecc, MR, ATMEL_ECC_PAGESIZE_528);
			break;
		case 1024:
			nand_chip->ecc.layout = &atmel_oobinfo_large;
			ecc_writel(host->ecc, MR, ATMEL_ECC_PAGESIZE_1056);
			break;
		case 2048:
			nand_chip->ecc.layout = &atmel_oobinfo_large;
			ecc_writel(host->ecc, MR, ATMEL_ECC_PAGESIZE_2112);
			break;
		case 4096:
			nand_chip->ecc.layout = &atmel_oobinfo_large;
			ecc_writel(host->ecc, MR, ATMEL_ECC_PAGESIZE_4224);
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
		goto err_scan_tail;
	}

#ifdef CONFIG_MTD_PARTITIONS
#ifdef CONFIG_MTD_CMDLINE_PARTS
	mtd->name = "atmel_nand";
	num_partitions = parse_mtd_partitions(mtd, part_probes,
					      &partitions, 0);
#endif
	if (num_partitions <= 0 && host->board->partition_info)
		partitions = host->board->partition_info(mtd->size,
							 &num_partitions);

	if ((!partitions) || (num_partitions == 0)) {
		printk(KERN_ERR "atmel_nand: No partitions defined, or unsupported device.\n");
		res = ENXIO;
		goto err_no_partitions;
	}

	res = add_mtd_partitions(mtd, partitions, num_partitions);
#else
	res = add_mtd_device(mtd);
#endif

	if (!res)
		return res;

#ifdef CONFIG_MTD_PARTITIONS
err_no_partitions:
#endif
	nand_release(mtd);
err_scan_tail:
err_scan_ident:
err_no_card:
	atmel_nand_disable(host);
	platform_set_drvdata(pdev, NULL);
	if (host->ecc)
		iounmap(host->ecc);
err_ecc_ioremap:
	iounmap(host->io_base);
err_nand_ioremap:
	kfree(host);
	return res;
}

/*
 * Remove a NAND device.
 */
static int __exit atmel_nand_remove(struct platform_device *pdev)
{
	struct atmel_nand_host *host = platform_get_drvdata(pdev);
	struct mtd_info *mtd = &host->mtd;

	nand_release(mtd);

	atmel_nand_disable(host);

	if (host->ecc)
		iounmap(host->ecc);
	iounmap(host->io_base);
	kfree(host);

	return 0;
}

static struct platform_driver atmel_nand_driver = {
	.remove		= __exit_p(atmel_nand_remove),
	.driver		= {
		.name	= "atmel_nand",
		.owner	= THIS_MODULE,
	},
};

static int __init atmel_nand_init(void)
{
	return platform_driver_probe(&atmel_nand_driver, atmel_nand_probe);
}


static void __exit atmel_nand_exit(void)
{
	platform_driver_unregister(&atmel_nand_driver);
}


module_init(atmel_nand_init);
module_exit(atmel_nand_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Rick Bronson");
MODULE_DESCRIPTION("NAND/SmartMedia driver for AT91 / AVR32");
MODULE_ALIAS("platform:atmel_nand");
