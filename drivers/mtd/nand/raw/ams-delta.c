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
#include <linux/mtd/nand-gpio.h>
#include <linux/mtd/rawnand.h>
#include <linux/mtd/partitions.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/sizes.h>

/*
 * MTD structure for E3 (Delta)
 */
struct gpio_nand {
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
	unsigned int		tRP;
	unsigned int		tWP;
	u8			(*io_read)(struct gpio_nand *this);
	void			(*io_write)(struct gpio_nand *this, u8 byte);
};

static void gpio_nand_write_commit(struct gpio_nand *priv)
{
	gpiod_set_value(priv->gpiod_nwe, 1);
	ndelay(priv->tWP);
	gpiod_set_value(priv->gpiod_nwe, 0);
}

static void gpio_nand_io_write(struct gpio_nand *priv, u8 byte)
{
	struct gpio_descs *data_gpiods = priv->data_gpiods;
	DECLARE_BITMAP(values, BITS_PER_TYPE(byte)) = { byte, };

	gpiod_set_raw_array_value(data_gpiods->ndescs, data_gpiods->desc,
				  data_gpiods->info, values);

	gpio_nand_write_commit(priv);
}

static void gpio_nand_dir_output(struct gpio_nand *priv, u8 byte)
{
	struct gpio_descs *data_gpiods = priv->data_gpiods;
	DECLARE_BITMAP(values, BITS_PER_TYPE(byte)) = { byte, };
	int i;

	for (i = 0; i < data_gpiods->ndescs; i++)
		gpiod_direction_output_raw(data_gpiods->desc[i],
					   test_bit(i, values));

	gpio_nand_write_commit(priv);

	priv->data_in = false;
}

static u8 gpio_nand_io_read(struct gpio_nand *priv)
{
	u8 res;
	struct gpio_descs *data_gpiods = priv->data_gpiods;
	DECLARE_BITMAP(values, BITS_PER_TYPE(res)) = { 0, };

	gpiod_set_value(priv->gpiod_nre, 1);
	ndelay(priv->tRP);

	gpiod_get_raw_array_value(data_gpiods->ndescs, data_gpiods->desc,
				  data_gpiods->info, values);

	gpiod_set_value(priv->gpiod_nre, 0);

	res = values[0];
	return res;
}

static void gpio_nand_dir_input(struct gpio_nand *priv)
{
	struct gpio_descs *data_gpiods = priv->data_gpiods;
	int i;

	for (i = 0; i < data_gpiods->ndescs; i++)
		gpiod_direction_input(data_gpiods->desc[i]);

	priv->data_in = true;
}

static void gpio_nand_write_buf(struct gpio_nand *priv, const u8 *buf, int len)
{
	int i = 0;

	if (len > 0 && priv->data_in)
		gpio_nand_dir_output(priv, buf[i++]);

	while (i < len)
		priv->io_write(priv, buf[i++]);
}

static void gpio_nand_read_buf(struct gpio_nand *priv, u8 *buf, int len)
{
	int i;

	if (priv->data_gpiods && !priv->data_in)
		gpio_nand_dir_input(priv);

	for (i = 0; i < len; i++)
		buf[i] = priv->io_read(priv);
}

static void gpio_nand_ctrl_cs(struct gpio_nand *priv, bool assert)
{
	gpiod_set_value(priv->gpiod_nce, assert);
}

static int gpio_nand_exec_op(struct nand_chip *this,
			     const struct nand_operation *op, bool check_only)
{
	struct gpio_nand *priv = nand_get_controller_data(this);
	const struct nand_op_instr *instr;
	int ret = 0;

	if (check_only)
		return 0;

	gpio_nand_ctrl_cs(priv, 1);

	for (instr = op->instrs; instr < op->instrs + op->ninstrs; instr++) {
		switch (instr->type) {
		case NAND_OP_CMD_INSTR:
			gpiod_set_value(priv->gpiod_cle, 1);
			gpio_nand_write_buf(priv, &instr->ctx.cmd.opcode, 1);
			gpiod_set_value(priv->gpiod_cle, 0);
			break;

		case NAND_OP_ADDR_INSTR:
			gpiod_set_value(priv->gpiod_ale, 1);
			gpio_nand_write_buf(priv, instr->ctx.addr.addrs,
					    instr->ctx.addr.naddrs);
			gpiod_set_value(priv->gpiod_ale, 0);
			break;

		case NAND_OP_DATA_IN_INSTR:
			gpio_nand_read_buf(priv, instr->ctx.data.buf.in,
					   instr->ctx.data.len);
			break;

		case NAND_OP_DATA_OUT_INSTR:
			gpio_nand_write_buf(priv, instr->ctx.data.buf.out,
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

	gpio_nand_ctrl_cs(priv, 0);

	return ret;
}

static int gpio_nand_setup_data_interface(struct nand_chip *this, int csline,
					  const struct nand_data_interface *cf)
{
	struct gpio_nand *priv = nand_get_controller_data(this);
	const struct nand_sdr_timings *sdr = nand_get_sdr_timings(cf);
	struct device *dev = &nand_to_mtd(this)->dev;

	if (IS_ERR(sdr))
		return PTR_ERR(sdr);

	if (csline == NAND_DATA_IFACE_CHECK_ONLY)
		return 0;

	if (priv->gpiod_nre) {
		priv->tRP = DIV_ROUND_UP(sdr->tRP_min, 1000);
		dev_dbg(dev, "using %u ns read pulse width\n", priv->tRP);
	}

	priv->tWP = DIV_ROUND_UP(sdr->tWP_min, 1000);
	dev_dbg(dev, "using %u ns write pulse width\n", priv->tWP);

	return 0;
}

static const struct nand_controller_ops gpio_nand_ops = {
	.exec_op = gpio_nand_exec_op,
	.setup_data_interface = gpio_nand_setup_data_interface,
};

/*
 * Main initialization routine
 */
static int gpio_nand_probe(struct platform_device *pdev)
{
	struct gpio_nand_platdata *pdata = dev_get_platdata(&pdev->dev);
	const struct mtd_partition *partitions = NULL;
	int num_partitions = 0;
	struct gpio_nand *priv;
	struct nand_chip *this;
	struct mtd_info *mtd;
	int (*probe)(struct platform_device *pdev, struct gpio_nand *priv);
	int err = 0;

	if (pdata) {
		partitions = pdata->parts;
		num_partitions = pdata->num_parts;
	}

	/* Allocate memory for MTD device structure and private data */
	priv = devm_kzalloc(&pdev->dev, sizeof(struct gpio_nand),
			    GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	this = &priv->nand_chip;

	mtd = nand_to_mtd(this);
	mtd->dev.parent = &pdev->dev;

	nand_set_controller_data(this, priv);
	nand_set_flash_node(this, pdev->dev.of_node);

	priv->gpiod_rdy = devm_gpiod_get_optional(&pdev->dev, "rdy", GPIOD_IN);
	if (IS_ERR(priv->gpiod_rdy)) {
		err = PTR_ERR(priv->gpiod_rdy);
		dev_warn(&pdev->dev, "RDY GPIO request failed (%d)\n", err);
		return err;
	}

	this->ecc.mode = NAND_ECC_SOFT;
	this->ecc.algo = NAND_ECC_HAMMING;

	platform_set_drvdata(pdev, priv);

	/* Set chip enabled but write protected */
	priv->gpiod_nwp = devm_gpiod_get_optional(&pdev->dev, "nwp",
						  GPIOD_OUT_HIGH);
	if (IS_ERR(priv->gpiod_nwp)) {
		err = PTR_ERR(priv->gpiod_nwp);
		dev_err(&pdev->dev, "NWP GPIO request failed (%d)\n", err);
		return err;
	}

	priv->gpiod_nce = devm_gpiod_get_optional(&pdev->dev, "nce",
						  GPIOD_OUT_LOW);
	if (IS_ERR(priv->gpiod_nce)) {
		err = PTR_ERR(priv->gpiod_nce);
		dev_err(&pdev->dev, "NCE GPIO request failed (%d)\n", err);
		return err;
	}

	priv->gpiod_nre = devm_gpiod_get_optional(&pdev->dev, "nre",
						  GPIOD_OUT_LOW);
	if (IS_ERR(priv->gpiod_nre)) {
		err = PTR_ERR(priv->gpiod_nre);
		dev_err(&pdev->dev, "NRE GPIO request failed (%d)\n", err);
		return err;
	}

	priv->gpiod_nwe = devm_gpiod_get_optional(&pdev->dev, "nwe",
						  GPIOD_OUT_LOW);
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
	priv->data_gpiods = devm_gpiod_get_array_optional(&pdev->dev, "data",
							  GPIOD_IN);
	if (IS_ERR(priv->data_gpiods)) {
		err = PTR_ERR(priv->data_gpiods);
		dev_err(&pdev->dev, "data GPIO request failed: %d\n", err);
		return err;
	}
	if (priv->data_gpiods) {
		if (!priv->gpiod_nwe) {
			dev_err(&pdev->dev,
				"mandatory NWE pin not provided by platform\n");
			return -ENODEV;
		}

		priv->io_read = gpio_nand_io_read;
		priv->io_write = gpio_nand_io_write;
		priv->data_in = true;
	}

	if (pdev->id_entry)
		probe = (void *) pdev->id_entry->driver_data;
	else
		probe = of_device_get_match_data(&pdev->dev);
	if (probe)
		err = probe(pdev, priv);
	if (err)
		return err;

	if (!priv->io_read || !priv->io_write) {
		dev_err(&pdev->dev, "incomplete device configuration\n");
		return -ENODEV;
	}

	/* Initialize the NAND controller object embedded in gpio_nand. */
	priv->base.ops = &gpio_nand_ops;
	nand_controller_init(&priv->base);
	this->controller = &priv->base;

	/*
	 * FIXME: We should release write protection only after nand_scan() to
	 * be on the safe side but we can't do that until we have a generic way
	 * to assert/deassert WP from the core.  Even if the core shouldn't
	 * write things in the nand_scan() path, it should have control on this
	 * pin just in case we ever need to disable write protection during
	 * chip detection/initialization.
	 */
	/* Release write protection */
	gpiod_set_value(priv->gpiod_nwp, 0);

	/* Scan to find existence of the device */
	err = nand_scan(this, 1);
	if (err)
		return err;

	/* Register the partitions */
	err = mtd_device_register(mtd, partitions, num_partitions);
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
static int gpio_nand_remove(struct platform_device *pdev)
{
	struct gpio_nand *priv = platform_get_drvdata(pdev);
	struct mtd_info *mtd = nand_to_mtd(&priv->nand_chip);

	/* Apply write protection */
	gpiod_set_value(priv->gpiod_nwp, 1);

	/* Unregister device */
	nand_release(mtd_to_nand(mtd));

	return 0;
}

static const struct of_device_id gpio_nand_of_id_table[] = {
	{
		/* sentinel */
	},
};
MODULE_DEVICE_TABLE(of, gpio_nand_of_id_table);

static const struct platform_device_id gpio_nand_plat_id_table[] = {
	{
		.name	= "ams-delta-nand",
	}, {
		/* sentinel */
	},
};
MODULE_DEVICE_TABLE(platform, gpio_nand_plat_id_table);

static struct platform_driver gpio_nand_driver = {
	.probe		= gpio_nand_probe,
	.remove		= gpio_nand_remove,
	.id_table	= gpio_nand_plat_id_table,
	.driver		= {
		.name	= "ams-delta-nand",
		.of_match_table = of_match_ptr(gpio_nand_of_id_table),
	},
};

module_platform_driver(gpio_nand_driver);

MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Jonathan McDowell <noodles@earth.li>");
MODULE_DESCRIPTION("Glue layer for NAND flash on Amstrad E3 (Delta)");
