/*
 *  Copyright (C) 2006 Jonathan McDowell <noodles@earth.li>
 *
 *  Derived from drivers/mtd/nand/toto.c (removed in v2.6.28)
 *    Copyright (c) 2003 Texas Instruments
 *    Copyright (c) 2002 Thomas Gleixner <tgxl@linutronix.de>
 *
 *  Converted to platform driver by Janusz Krzysztofik <jkrzyszt@tis.icnet.pl>
 *  Partially stolen from plat_nand.c
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 *  Overview:
 *   This is a device driver for the NAND flash device found on the
 *   Amstrad E3 (Delta).
 */

#include <linux/slab.h>
#include <linux/module.h>
#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/mtd/mtd.h>
#include <linux/mtd/rawnand.h>
#include <linux/mtd/partitions.h>
#include <linux/platform_data/gpio-omap.h>

#include <asm/io.h>
#include <asm/sizes.h>

#include <mach/hardware.h>

/*
 * MTD structure for E3 (Delta)
 */
static struct mtd_info *ams_delta_mtd = NULL;
static struct gpio_desc *gpiod_rdy;
static struct gpio_desc *gpiod_nce;
static struct gpio_desc *gpiod_nre;
static struct gpio_desc *gpiod_nwp;
static struct gpio_desc *gpiod_nwe;
static struct gpio_desc *gpiod_ale;
static struct gpio_desc *gpiod_cle;

/*
 * Define partitions for flash devices
 */

static const struct mtd_partition partition_info[] = {
	{ .name		= "Kernel",
	  .offset	= 0,
	  .size		= 3 * SZ_1M + SZ_512K },
	{ .name		= "u-boot",
	  .offset	= 3 * SZ_1M + SZ_512K,
	  .size		= SZ_256K },
	{ .name		= "u-boot params",
	  .offset	= 3 * SZ_1M + SZ_512K + SZ_256K,
	  .size		= SZ_256K },
	{ .name		= "Amstrad LDR",
	  .offset	= 4 * SZ_1M,
	  .size		= SZ_256K },
	{ .name		= "File system",
	  .offset	= 4 * SZ_1M + 1 * SZ_256K,
	  .size		= 27 * SZ_1M },
	{ .name		= "PBL reserved",
	  .offset	= 32 * SZ_1M - 3 * SZ_256K,
	  .size		=  3 * SZ_256K },
};

static void ams_delta_write_byte(struct nand_chip *this, u_char byte)
{
	void __iomem *io_base = (void __iomem *)nand_get_controller_data(this);

	writew(0, io_base + OMAP_MPUIO_IO_CNTL);
	writew(byte, this->legacy.IO_ADDR_W);
	gpiod_set_value(gpiod_nwe, 0);
	ndelay(40);
	gpiod_set_value(gpiod_nwe, 1);
}

static u_char ams_delta_read_byte(struct nand_chip *this)
{
	u_char res;
	void __iomem *io_base = (void __iomem *)nand_get_controller_data(this);

	gpiod_set_value(gpiod_nre, 0);
	ndelay(40);
	writew(~0, io_base + OMAP_MPUIO_IO_CNTL);
	res = readw(this->legacy.IO_ADDR_R);
	gpiod_set_value(gpiod_nre, 1);

	return res;
}

static void ams_delta_write_buf(struct nand_chip *this, const u_char *buf,
				int len)
{
	int i;

	for (i=0; i<len; i++)
		ams_delta_write_byte(this, buf[i]);
}

static void ams_delta_read_buf(struct nand_chip *this, u_char *buf, int len)
{
	int i;

	for (i=0; i<len; i++)
		buf[i] = ams_delta_read_byte(this);
}

/*
 * Command control function
 *
 * ctrl:
 * NAND_NCE: bit 0 -> bit 2
 * NAND_CLE: bit 1 -> bit 7
 * NAND_ALE: bit 2 -> bit 6
 */
static void ams_delta_hwcontrol(struct nand_chip *this, int cmd,
				unsigned int ctrl)
{

	if (ctrl & NAND_CTRL_CHANGE) {
		gpiod_set_value(gpiod_nce, !(ctrl & NAND_NCE));
		gpiod_set_value(gpiod_cle, !!(ctrl & NAND_CLE));
		gpiod_set_value(gpiod_ale, !!(ctrl & NAND_ALE));
	}

	if (cmd != NAND_CMD_NONE)
		ams_delta_write_byte(this, cmd);
}

static int ams_delta_nand_ready(struct nand_chip *this)
{
	return gpiod_get_value(gpiod_rdy);
}


/*
 * Main initialization routine
 */
static int ams_delta_init(struct platform_device *pdev)
{
	struct nand_chip *this;
	struct resource *res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	void __iomem *io_base;
	int err = 0;

	if (!res)
		return -ENXIO;

	/* Allocate memory for MTD device structure and private data */
	this = kzalloc(sizeof(struct nand_chip), GFP_KERNEL);
	if (!this) {
		pr_warn("Unable to allocate E3 NAND MTD device structure.\n");
		err = -ENOMEM;
		goto out;
	}

	ams_delta_mtd = nand_to_mtd(this);
	ams_delta_mtd->owner = THIS_MODULE;

	/*
	 * Don't try to request the memory region from here,
	 * it should have been already requested from the
	 * gpio-omap driver and requesting it again would fail.
	 */

	io_base = ioremap(res->start, resource_size(res));
	if (io_base == NULL) {
		dev_err(&pdev->dev, "ioremap failed\n");
		err = -EIO;
		goto out_free;
	}

	nand_set_controller_data(this, (void *)io_base);

	/* Set address of NAND IO lines */
	this->legacy.IO_ADDR_R = io_base + OMAP_MPUIO_INPUT_LATCH;
	this->legacy.IO_ADDR_W = io_base + OMAP_MPUIO_OUTPUT;
	this->legacy.read_byte = ams_delta_read_byte;
	this->legacy.write_buf = ams_delta_write_buf;
	this->legacy.read_buf = ams_delta_read_buf;
	this->legacy.cmd_ctrl = ams_delta_hwcontrol;

	gpiod_rdy = devm_gpiod_get_optional(&pdev->dev, "rdy", GPIOD_IN);
	if (IS_ERR(gpiod_rdy)) {
		err = PTR_ERR(gpiod_rdy);
		dev_warn(&pdev->dev, "RDY GPIO request failed (%d)\n", err);
		goto out_mtd;
	}

	if (gpiod_rdy)
		this->legacy.dev_ready = ams_delta_nand_ready;

	/* 25 us command delay time */
	this->legacy.chip_delay = 30;
	this->ecc.mode = NAND_ECC_SOFT;
	this->ecc.algo = NAND_ECC_HAMMING;

	platform_set_drvdata(pdev, io_base);

	/* Set chip enabled, but  */
	gpiod_nwp = devm_gpiod_get(&pdev->dev, "nwp", GPIOD_OUT_HIGH);
	if (IS_ERR(gpiod_nwp)) {
		err = PTR_ERR(gpiod_nwp);
		dev_err(&pdev->dev, "NWP GPIO request failed (%d)\n", err);
		goto out_mtd;
	}

	gpiod_nce = devm_gpiod_get(&pdev->dev, "nce", GPIOD_OUT_HIGH);
	if (IS_ERR(gpiod_nce)) {
		err = PTR_ERR(gpiod_nce);
		dev_err(&pdev->dev, "NCE GPIO request failed (%d)\n", err);
		goto out_mtd;
	}

	gpiod_nre = devm_gpiod_get(&pdev->dev, "nre", GPIOD_OUT_HIGH);
	if (IS_ERR(gpiod_nre)) {
		err = PTR_ERR(gpiod_nre);
		dev_err(&pdev->dev, "NRE GPIO request failed (%d)\n", err);
		goto out_mtd;
	}

	gpiod_nwe = devm_gpiod_get(&pdev->dev, "nwe", GPIOD_OUT_HIGH);
	if (IS_ERR(gpiod_nwe)) {
		err = PTR_ERR(gpiod_nwe);
		dev_err(&pdev->dev, "NWE GPIO request failed (%d)\n", err);
		goto out_mtd;
	}

	gpiod_ale = devm_gpiod_get(&pdev->dev, "ale", GPIOD_OUT_LOW);
	if (IS_ERR(gpiod_ale)) {
		err = PTR_ERR(gpiod_ale);
		dev_err(&pdev->dev, "ALE GPIO request failed (%d)\n", err);
		goto out_mtd;
	}

	gpiod_cle = devm_gpiod_get(&pdev->dev, "cle", GPIOD_OUT_LOW);
	if (IS_ERR(gpiod_cle)) {
		err = PTR_ERR(gpiod_cle);
		dev_err(&pdev->dev, "CLE GPIO request failed (%d)\n", err);
		goto out_mtd;
	}

	/* Scan to find existence of the device */
	err = nand_scan(this, 1);
	if (err)
		goto out_mtd;

	/* Register the partitions */
	mtd_device_register(ams_delta_mtd, partition_info,
			    ARRAY_SIZE(partition_info));

	goto out;

 out_mtd:
	iounmap(io_base);
out_free:
	kfree(this);
 out:
	return err;
}

/*
 * Clean up routine
 */
static int ams_delta_cleanup(struct platform_device *pdev)
{
	void __iomem *io_base = platform_get_drvdata(pdev);

	/* Release resources, unregister device */
	nand_release(mtd_to_nand(ams_delta_mtd));

	iounmap(io_base);

	/* Free the MTD device structure */
	kfree(mtd_to_nand(ams_delta_mtd));

	return 0;
}

static struct platform_driver ams_delta_nand_driver = {
	.probe		= ams_delta_init,
	.remove		= ams_delta_cleanup,
	.driver		= {
		.name	= "ams-delta-nand",
	},
};

module_platform_driver(ams_delta_nand_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Jonathan McDowell <noodles@earth.li>");
MODULE_DESCRIPTION("Glue layer for NAND flash on Amstrad E3 (Delta)");
