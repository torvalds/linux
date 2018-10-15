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

struct ams_delta_nand {
	struct nand_chip	nand_chip;
	struct gpio_desc	*gpiod_rdy;
	struct gpio_desc	*gpiod_nce;
	struct gpio_desc	*gpiod_nre;
	struct gpio_desc	*gpiod_nwp;
	struct gpio_desc	*gpiod_nwe;
	struct gpio_desc	*gpiod_ale;
	struct gpio_desc	*gpiod_cle;
	void __iomem		*io_base;
	bool			data_in;
};

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

static void ams_delta_io_write(struct ams_delta_nand *priv, u_char byte)
{
	writew(byte, priv->io_base + OMAP_MPUIO_OUTPUT);
	gpiod_set_value(priv->gpiod_nwe, 0);
	ndelay(40);
	gpiod_set_value(priv->gpiod_nwe, 1);
}

static u_char ams_delta_io_read(struct ams_delta_nand *priv)
{
	u_char res;

	gpiod_set_value(priv->gpiod_nre, 0);
	ndelay(40);
	res = readw(priv->io_base + OMAP_MPUIO_INPUT_LATCH);
	gpiod_set_value(priv->gpiod_nre, 1);

	return res;
}

static void ams_delta_dir_input(struct ams_delta_nand *priv, bool in)
{
	writew(in ? ~0 : 0, priv->io_base + OMAP_MPUIO_IO_CNTL);
	priv->data_in = in;
}

static void ams_delta_write_buf(struct ams_delta_nand *priv, const u_char *buf,
				int len)
{
	int i;

	if (priv->data_in)
		ams_delta_dir_input(priv, false);

	for (i = 0; i < len; i++)
		ams_delta_io_write(priv, buf[i]);
}

static void ams_delta_read_buf(struct ams_delta_nand *priv, u_char *buf,
			       int len)
{
	int i;

	if (!priv->data_in)
		ams_delta_dir_input(priv, true);

	for (i = 0; i < len; i++)
		buf[i] = ams_delta_io_read(priv);
}

static void ams_delta_select_chip(struct nand_chip *this, int n)
{
	struct ams_delta_nand *priv = nand_get_controller_data(this);

	if (n > 0)
		return;

	gpiod_set_value(priv->gpiod_nce, n < 0);
}

static int ams_delta_exec_op(struct nand_chip *this,
			     const struct nand_operation *op, bool check_only)
{
	struct ams_delta_nand *priv = nand_get_controller_data(this);
	const struct nand_op_instr *instr;
	int ret = 0;

	if (check_only)
		return 0;

	for (instr = op->instrs; instr < op->instrs + op->ninstrs; instr++) {

		switch (instr->type) {
		case NAND_OP_CMD_INSTR:
			gpiod_set_value(priv->gpiod_cle, 1);
			ams_delta_write_buf(priv, &instr->ctx.cmd.opcode, 1);
			gpiod_set_value(priv->gpiod_cle, 0);
			break;

		case NAND_OP_ADDR_INSTR:
			gpiod_set_value(priv->gpiod_ale, 1);
			ams_delta_write_buf(priv, instr->ctx.addr.addrs,
					    instr->ctx.addr.naddrs);
			gpiod_set_value(priv->gpiod_ale, 0);
			break;

		case NAND_OP_DATA_IN_INSTR:
			ams_delta_read_buf(priv, instr->ctx.data.buf.in,
					   instr->ctx.data.len);
			break;

		case NAND_OP_DATA_OUT_INSTR:
			ams_delta_write_buf(priv, instr->ctx.data.buf.out,
					    instr->ctx.data.len);
			break;

		case NAND_OP_WAITRDY_INSTR:
			ret = priv->gpiod_rdy ?
			      nand_gpio_waitrdy(this, priv->gpiod_rdy,
						instr->ctx.waitrdy.timeout_ms) :
			      nand_soft_waitrdy(this,
						instr->ctx.waitrdy.timeout_ms);
			break;
		}

		if (ret)
			break;
	}

	return ret;
}


/*
 * Main initialization routine
 */
static int ams_delta_init(struct platform_device *pdev)
{
	struct ams_delta_nand *priv;
	struct nand_chip *this;
	struct mtd_info *mtd;
	struct resource *res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	void __iomem *io_base;
	int err = 0;

	if (!res)
		return -ENXIO;

	/* Allocate memory for MTD device structure and private data */
	priv = devm_kzalloc(&pdev->dev, sizeof(struct ams_delta_nand),
			    GFP_KERNEL);
	if (!priv) {
		pr_warn("Unable to allocate E3 NAND MTD device structure.\n");
		return -ENOMEM;
	}
	this = &priv->nand_chip;

	mtd = nand_to_mtd(this);
	mtd->dev.parent = &pdev->dev;

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

	priv->io_base = io_base;
	nand_set_controller_data(this, priv);

	/* Set address of NAND IO lines */
	this->select_chip = ams_delta_select_chip;
	this->exec_op = ams_delta_exec_op;

	priv->gpiod_rdy = devm_gpiod_get_optional(&pdev->dev, "rdy", GPIOD_IN);
	if (IS_ERR(priv->gpiod_rdy)) {
		err = PTR_ERR(priv->gpiod_rdy);
		dev_warn(&pdev->dev, "RDY GPIO request failed (%d)\n", err);
		goto out_mtd;
	}

	this->ecc.mode = NAND_ECC_SOFT;
	this->ecc.algo = NAND_ECC_HAMMING;

	platform_set_drvdata(pdev, priv);

	/* Set chip enabled, but  */
	priv->gpiod_nwp = devm_gpiod_get(&pdev->dev, "nwp", GPIOD_OUT_HIGH);
	if (IS_ERR(priv->gpiod_nwp)) {
		err = PTR_ERR(priv->gpiod_nwp);
		dev_err(&pdev->dev, "NWP GPIO request failed (%d)\n", err);
		goto out_mtd;
	}

	priv->gpiod_nce = devm_gpiod_get(&pdev->dev, "nce", GPIOD_OUT_HIGH);
	if (IS_ERR(priv->gpiod_nce)) {
		err = PTR_ERR(priv->gpiod_nce);
		dev_err(&pdev->dev, "NCE GPIO request failed (%d)\n", err);
		goto out_mtd;
	}

	priv->gpiod_nre = devm_gpiod_get(&pdev->dev, "nre", GPIOD_OUT_HIGH);
	if (IS_ERR(priv->gpiod_nre)) {
		err = PTR_ERR(priv->gpiod_nre);
		dev_err(&pdev->dev, "NRE GPIO request failed (%d)\n", err);
		goto out_mtd;
	}

	priv->gpiod_nwe = devm_gpiod_get(&pdev->dev, "nwe", GPIOD_OUT_HIGH);
	if (IS_ERR(priv->gpiod_nwe)) {
		err = PTR_ERR(priv->gpiod_nwe);
		dev_err(&pdev->dev, "NWE GPIO request failed (%d)\n", err);
		goto out_mtd;
	}

	priv->gpiod_ale = devm_gpiod_get(&pdev->dev, "ale", GPIOD_OUT_LOW);
	if (IS_ERR(priv->gpiod_ale)) {
		err = PTR_ERR(priv->gpiod_ale);
		dev_err(&pdev->dev, "ALE GPIO request failed (%d)\n", err);
		goto out_mtd;
	}

	priv->gpiod_cle = devm_gpiod_get(&pdev->dev, "cle", GPIOD_OUT_LOW);
	if (IS_ERR(priv->gpiod_cle)) {
		err = PTR_ERR(priv->gpiod_cle);
		dev_err(&pdev->dev, "CLE GPIO request failed (%d)\n", err);
		goto out_mtd;
	}

	/* Initialize data port direction to a known state */
	ams_delta_dir_input(priv, true);

	/* Scan to find existence of the device */
	err = nand_scan(this, 1);
	if (err)
		goto out_mtd;

	/* Register the partitions */
	mtd_device_register(mtd, partition_info, ARRAY_SIZE(partition_info));

	goto out;

 out_mtd:
	iounmap(io_base);
out_free:
 out:
	return err;
}

/*
 * Clean up routine
 */
static int ams_delta_cleanup(struct platform_device *pdev)
{
	struct ams_delta_nand *priv = platform_get_drvdata(pdev);
	struct mtd_info *mtd = nand_to_mtd(&priv->nand_chip);
	void __iomem *io_base = priv->io_base;

	/* Release resources, unregister device */
	nand_release(mtd_to_nand(mtd));

	iounmap(io_base);

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
