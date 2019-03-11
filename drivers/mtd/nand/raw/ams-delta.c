// SPDX-License-Identifier: GPL-2.0
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
#include <linux/platform_device.h>
#include <linux/sizes.h>

/*
 * MTD structure for E3 (Delta)
 */
struct ams_delta_nand {
	struct nand_controller	base;
	struct nand_chip	nand_chip;
	struct gpio_desc	*gpiod_rdy;
	struct gpio_desc	*gpiod_nce;
	struct gpio_desc	*gpiod_nre;
	struct gpio_desc	*gpiod_nwp;
	struct gpio_desc	*gpiod_nwe;
	struct gpio_desc	*gpiod_ale;
	struct gpio_desc	*gpiod_cle;
	struct gpio_descs	*data_gpiods;
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

static void ams_delta_write_commit(struct ams_delta_nand *priv)
{
	gpiod_set_value(priv->gpiod_nwe, 0);
	ndelay(40);
	gpiod_set_value(priv->gpiod_nwe, 1);
}

static void ams_delta_io_write(struct ams_delta_nand *priv, u8 byte)
{
	struct gpio_descs *data_gpiods = priv->data_gpiods;
	DECLARE_BITMAP(values, BITS_PER_TYPE(byte)) = { byte, };

	gpiod_set_raw_array_value(data_gpiods->ndescs, data_gpiods->desc,
				  data_gpiods->info, values);

	ams_delta_write_commit(priv);
}

static void ams_delta_dir_output(struct ams_delta_nand *priv, u8 byte)
{
	struct gpio_descs *data_gpiods = priv->data_gpiods;
	DECLARE_BITMAP(values, BITS_PER_TYPE(byte)) = { byte, };
	int i;

	for (i = 0; i < data_gpiods->ndescs; i++)
		gpiod_direction_output_raw(data_gpiods->desc[i],
					   test_bit(i, values));

	ams_delta_write_commit(priv);

	priv->data_in = false;
}

static u8 ams_delta_io_read(struct ams_delta_nand *priv)
{
	u8 res;
	struct gpio_descs *data_gpiods = priv->data_gpiods;
	DECLARE_BITMAP(values, BITS_PER_TYPE(res)) = { 0, };

	gpiod_set_value(priv->gpiod_nre, 0);
	ndelay(40);

	gpiod_get_raw_array_value(data_gpiods->ndescs, data_gpiods->desc,
				  data_gpiods->info, values);

	gpiod_set_value(priv->gpiod_nre, 1);

	res = values[0];
	return res;
}

static void ams_delta_dir_input(struct ams_delta_nand *priv)
{
	struct gpio_descs *data_gpiods = priv->data_gpiods;
	int i;

	for (i = 0; i < data_gpiods->ndescs; i++)
		gpiod_direction_input(data_gpiods->desc[i]);

	priv->data_in = true;
}

static void ams_delta_write_buf(struct ams_delta_nand *priv, const u8 *buf,
				int len)
{
	int i = 0;

	if (len > 0 && priv->data_in)
		ams_delta_dir_output(priv, buf[i++]);

	while (i < len)
		ams_delta_io_write(priv, buf[i++]);
}

static void ams_delta_read_buf(struct ams_delta_nand *priv, u8 *buf, int len)
{
	int i;

	if (!priv->data_in)
		ams_delta_dir_input(priv);

	for (i = 0; i < len; i++)
		buf[i] = ams_delta_io_read(priv);
}

static void ams_delta_ctrl_cs(struct ams_delta_nand *priv, bool assert)
{
	gpiod_set_value(priv->gpiod_nce, assert ? 0 : 1);
}

static int ams_delta_exec_op(struct nand_chip *this,
			     const struct nand_operation *op, bool check_only)
{
	struct ams_delta_nand *priv = nand_get_controller_data(this);
	const struct nand_op_instr *instr;
	int ret = 0;

	if (check_only)
		return 0;

	ams_delta_ctrl_cs(priv, 1);

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

	ams_delta_ctrl_cs(priv, 0);

	return ret;
}

static const struct nand_controller_ops ams_delta_ops = {
	.exec_op = ams_delta_exec_op,
};

/*
 * Main initialization routine
 */
static int ams_delta_init(struct platform_device *pdev)
{
	struct ams_delta_nand *priv;
	struct nand_chip *this;
	struct mtd_info *mtd;
	struct gpio_descs *data_gpiods;
	int err = 0;

	/* Allocate memory for MTD device structure and private data */
	priv = devm_kzalloc(&pdev->dev, sizeof(struct ams_delta_nand),
			    GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	this = &priv->nand_chip;

	mtd = nand_to_mtd(this);
	mtd->dev.parent = &pdev->dev;

	nand_set_controller_data(this, priv);

	priv->gpiod_rdy = devm_gpiod_get_optional(&pdev->dev, "rdy", GPIOD_IN);
	if (IS_ERR(priv->gpiod_rdy)) {
		err = PTR_ERR(priv->gpiod_rdy);
		dev_warn(&pdev->dev, "RDY GPIO request failed (%d)\n", err);
		return err;
	}

	this->ecc.mode = NAND_ECC_SOFT;
	this->ecc.algo = NAND_ECC_HAMMING;

	platform_set_drvdata(pdev, priv);

	/* Set chip enabled, but  */
	priv->gpiod_nwp = devm_gpiod_get(&pdev->dev, "nwp", GPIOD_OUT_HIGH);
	if (IS_ERR(priv->gpiod_nwp)) {
		err = PTR_ERR(priv->gpiod_nwp);
		dev_err(&pdev->dev, "NWP GPIO request failed (%d)\n", err);
		return err;
	}

	priv->gpiod_nce = devm_gpiod_get(&pdev->dev, "nce", GPIOD_OUT_HIGH);
	if (IS_ERR(priv->gpiod_nce)) {
		err = PTR_ERR(priv->gpiod_nce);
		dev_err(&pdev->dev, "NCE GPIO request failed (%d)\n", err);
		return err;
	}

	priv->gpiod_nre = devm_gpiod_get(&pdev->dev, "nre", GPIOD_OUT_HIGH);
	if (IS_ERR(priv->gpiod_nre)) {
		err = PTR_ERR(priv->gpiod_nre);
		dev_err(&pdev->dev, "NRE GPIO request failed (%d)\n", err);
		return err;
	}

	priv->gpiod_nwe = devm_gpiod_get(&pdev->dev, "nwe", GPIOD_OUT_HIGH);
	if (IS_ERR(priv->gpiod_nwe)) {
		err = PTR_ERR(priv->gpiod_nwe);
		dev_err(&pdev->dev, "NWE GPIO request failed (%d)\n", err);
		return err;
	}

	priv->gpiod_ale = devm_gpiod_get(&pdev->dev, "ale", GPIOD_OUT_LOW);
	if (IS_ERR(priv->gpiod_ale)) {
		err = PTR_ERR(priv->gpiod_ale);
		dev_err(&pdev->dev, "ALE GPIO request failed (%d)\n", err);
		return err;
	}

	priv->gpiod_cle = devm_gpiod_get(&pdev->dev, "cle", GPIOD_OUT_LOW);
	if (IS_ERR(priv->gpiod_cle)) {
		err = PTR_ERR(priv->gpiod_cle);
		dev_err(&pdev->dev, "CLE GPIO request failed (%d)\n", err);
		return err;
	}

	/* Request array of data pins, initialize them as input */
	data_gpiods = devm_gpiod_get_array(&pdev->dev, "data", GPIOD_IN);
	if (IS_ERR(data_gpiods)) {
		err = PTR_ERR(data_gpiods);
		dev_err(&pdev->dev, "data GPIO request failed: %d\n", err);
		return err;
	}
	priv->data_gpiods = data_gpiods;
	priv->data_in = true;

	/* Initialize the NAND controller object embedded in ams_delta_nand. */
	priv->base.ops = &ams_delta_ops;
	nand_controller_init(&priv->base);
	this->controller = &priv->base;

	/* Scan to find existence of the device */
	err = nand_scan(this, 1);
	if (err)
		return err;

	/* Register the partitions */
	err = mtd_device_register(mtd, partition_info,
				  ARRAY_SIZE(partition_info));
	if (err)
		goto err_nand_cleanup;

	return 0;

err_nand_cleanup:
	nand_cleanup(this);

	return err;
}

/*
 * Clean up routine
 */
static int ams_delta_cleanup(struct platform_device *pdev)
{
	struct ams_delta_nand *priv = platform_get_drvdata(pdev);
	struct mtd_info *mtd = nand_to_mtd(&priv->nand_chip);

	/* Unregister device */
	nand_release(mtd_to_nand(mtd));

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

MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Jonathan McDowell <noodles@earth.li>");
MODULE_DESCRIPTION("Glue layer for NAND flash on Amstrad E3 (Delta)");
