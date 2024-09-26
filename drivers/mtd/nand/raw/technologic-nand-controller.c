// SPDX-License-Identifier: GPL-2.0
/*
 * Technologic Systems TS72xx NAND controller driver
 *
 * Copyright (C) 2023 Nikita Shubin <nikita.shubin@maquefel.me>
 *
 * Derived from: plat_nand.c
 *  Author: Vitaly Wool <vitalywool@gmail.com>
 */

#include <linux/bits.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/iopoll.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/slab.h>

#include <linux/mtd/mtd.h>
#include <linux/mtd/platnand.h>

#define TS72XX_NAND_CONTROL_ADDR_LINE	BIT(22)	/* 0xN0400000 */
#define TS72XX_NAND_BUSY_ADDR_LINE	BIT(23)	/* 0xN0800000 */

#define TS72XX_NAND_ALE			BIT(0)
#define TS72XX_NAND_CLE			BIT(1)
#define TS72XX_NAND_NCE			BIT(2)

#define TS72XX_NAND_CTRL_CLE		(TS72XX_NAND_NCE | TS72XX_NAND_CLE)
#define TS72XX_NAND_CTRL_ALE		(TS72XX_NAND_NCE | TS72XX_NAND_ALE)

struct ts72xx_nand_data {
	struct nand_controller	controller;
	struct nand_chip	chip;
	void __iomem		*base;
	void __iomem		*ctrl;
	void __iomem		*busy;
};

static inline struct ts72xx_nand_data *chip_to_ts72xx(struct nand_chip *chip)
{
	return container_of(chip, struct ts72xx_nand_data, chip);
}

static int ts72xx_nand_attach_chip(struct nand_chip *chip)
{
	switch (chip->ecc.engine_type) {
	case NAND_ECC_ENGINE_TYPE_ON_HOST:
		return -EINVAL;
	case NAND_ECC_ENGINE_TYPE_SOFT:
		if (chip->ecc.algo == NAND_ECC_ALGO_UNKNOWN)
			chip->ecc.algo = NAND_ECC_ALGO_HAMMING;
		chip->ecc.algo = NAND_ECC_ALGO_HAMMING;
		fallthrough;
	default:
		return 0;
	}
}

static void ts72xx_nand_ctrl(struct nand_chip *chip, u8 value)
{
	struct ts72xx_nand_data *data = chip_to_ts72xx(chip);
	unsigned char bits = ioread8(data->ctrl) & ~GENMASK(2, 0);

	iowrite8(bits | value, data->ctrl);
}

static int ts72xx_nand_exec_instr(struct nand_chip *chip,
				const struct nand_op_instr *instr)
{
	struct ts72xx_nand_data *data = chip_to_ts72xx(chip);
	unsigned int timeout_us;
	u32 status;
	int ret;

	switch (instr->type) {
	case NAND_OP_CMD_INSTR:
		ts72xx_nand_ctrl(chip, TS72XX_NAND_CTRL_CLE);
		iowrite8(instr->ctx.cmd.opcode, data->base);
		ts72xx_nand_ctrl(chip, TS72XX_NAND_NCE);
		break;

	case NAND_OP_ADDR_INSTR:
		ts72xx_nand_ctrl(chip, TS72XX_NAND_CTRL_ALE);
		iowrite8_rep(data->base, instr->ctx.addr.addrs, instr->ctx.addr.naddrs);
		ts72xx_nand_ctrl(chip, TS72XX_NAND_NCE);
		break;

	case NAND_OP_DATA_IN_INSTR:
		ioread8_rep(data->base, instr->ctx.data.buf.in, instr->ctx.data.len);
		break;

	case NAND_OP_DATA_OUT_INSTR:
		iowrite8_rep(data->base, instr->ctx.data.buf.in, instr->ctx.data.len);
		break;

	case NAND_OP_WAITRDY_INSTR:
		timeout_us = instr->ctx.waitrdy.timeout_ms * 1000;
		ret = readb_poll_timeout(data->busy, status, status & BIT(5), 0, timeout_us);
		if (ret)
			return ret;

		break;
	}

	if (instr->delay_ns)
		ndelay(instr->delay_ns);

	return 0;
}

static int ts72xx_nand_exec_op(struct nand_chip *chip,
			       const struct nand_operation *op, bool check_only)
{
	unsigned int i;
	int ret;

	if (check_only)
		return 0;

	for (i = 0; i < op->ninstrs; i++) {
		ret = ts72xx_nand_exec_instr(chip, &op->instrs[i]);
		if (ret)
			return ret;
	}

	return 0;
}

static const struct nand_controller_ops ts72xx_nand_ops = {
	.attach_chip = ts72xx_nand_attach_chip,
	.exec_op = ts72xx_nand_exec_op,
};

static int ts72xx_nand_probe(struct platform_device *pdev)
{
	struct ts72xx_nand_data *data;
	struct fwnode_handle *child;
	struct mtd_info *mtd;
	int err;

	data = devm_kzalloc(&pdev->dev, sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	nand_controller_init(&data->controller);
	data->controller.ops = &ts72xx_nand_ops;
	data->chip.controller = &data->controller;

	data->base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(data->base))
		return PTR_ERR(data->base);
	data->ctrl = data->base + TS72XX_NAND_CONTROL_ADDR_LINE;
	data->busy = data->base + TS72XX_NAND_BUSY_ADDR_LINE;

	child = fwnode_get_next_child_node(dev_fwnode(&pdev->dev), NULL);
	if (!child)
		return dev_err_probe(&pdev->dev, -ENXIO,
				"ts72xx controller node should have exactly one child\n");

	nand_set_flash_node(&data->chip, to_of_node(child));
	mtd = nand_to_mtd(&data->chip);
	mtd->dev.parent = &pdev->dev;
	platform_set_drvdata(pdev, data);

	/*
	 * This driver assumes that the default ECC engine should be TYPE_SOFT.
	 * Set ->engine_type before registering the NAND devices in order to
	 * provide a driver specific default value.
	 */
	data->chip.ecc.engine_type = NAND_ECC_ENGINE_TYPE_SOFT;

	/* Scan to find existence of the device */
	err = nand_scan(&data->chip, 1);
	if (err)
		goto err_handle_put;

	err = mtd_device_parse_register(mtd, NULL, NULL, NULL, 0);
	if (err)
		goto err_clean_nand;

	return 0;

err_clean_nand:
	nand_cleanup(&data->chip);
err_handle_put:
	fwnode_handle_put(child);
	return err;
}

static void ts72xx_nand_remove(struct platform_device *pdev)
{
	struct ts72xx_nand_data *data = platform_get_drvdata(pdev);
	struct fwnode_handle *fwnode = dev_fwnode(&pdev->dev);
	struct nand_chip *chip = &data->chip;
	int ret;

	ret = mtd_device_unregister(nand_to_mtd(chip));
	WARN_ON(ret);
	nand_cleanup(chip);
	fwnode_handle_put(fwnode);
}

static const struct of_device_id ts72xx_id_table[] = {
	{ .compatible = "technologic,ts7200-nand" },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, ts72xx_id_table);

static struct platform_driver ts72xx_nand_driver = {
	.driver = {
		.name = "ts72xx-nand",
		.of_match_table = ts72xx_id_table,
	},
	.probe = ts72xx_nand_probe,
	.remove_new = ts72xx_nand_remove,
};
module_platform_driver(ts72xx_nand_driver);

MODULE_AUTHOR("Nikita Shubin <nikita.shubin@maquefel.me>");
MODULE_DESCRIPTION("Technologic Systems TS72xx NAND controller driver");
MODULE_LICENSE("GPL");
