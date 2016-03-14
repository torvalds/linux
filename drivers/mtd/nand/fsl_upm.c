/*
 * Freescale UPM NAND driver.
 *
 * Copyright © 2007-2008  MontaVista Software, Inc.
 *
 * Author: Anton Vorontsov <avorontsov@ru.mvista.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/delay.h>
#include <linux/mtd/nand.h>
#include <linux/mtd/nand_ecc.h>
#include <linux/mtd/partitions.h>
#include <linux/mtd/mtd.h>
#include <linux/of_address.h>
#include <linux/of_platform.h>
#include <linux/of_gpio.h>
#include <linux/io.h>
#include <linux/slab.h>
#include <asm/fsl_lbc.h>

#define FSL_UPM_WAIT_RUN_PATTERN  0x1
#define FSL_UPM_WAIT_WRITE_BYTE   0x2
#define FSL_UPM_WAIT_WRITE_BUFFER 0x4

struct fsl_upm_nand {
	struct device *dev;
	struct nand_chip chip;
	int last_ctrl;
	struct mtd_partition *parts;
	struct fsl_upm upm;
	uint8_t upm_addr_offset;
	uint8_t upm_cmd_offset;
	void __iomem *io_base;
	int rnb_gpio[NAND_MAX_CHIPS];
	uint32_t mchip_offsets[NAND_MAX_CHIPS];
	uint32_t mchip_count;
	uint32_t mchip_number;
	int chip_delay;
	uint32_t wait_flags;
};

static inline struct fsl_upm_nand *to_fsl_upm_nand(struct mtd_info *mtdinfo)
{
	return container_of(mtd_to_nand(mtdinfo), struct fsl_upm_nand,
			    chip);
}

static int fun_chip_ready(struct mtd_info *mtd)
{
	struct fsl_upm_nand *fun = to_fsl_upm_nand(mtd);

	if (gpio_get_value(fun->rnb_gpio[fun->mchip_number]))
		return 1;

	dev_vdbg(fun->dev, "busy\n");
	return 0;
}

static void fun_wait_rnb(struct fsl_upm_nand *fun)
{
	if (fun->rnb_gpio[fun->mchip_number] >= 0) {
		struct mtd_info *mtd = nand_to_mtd(&fun->chip);
		int cnt = 1000000;

		while (--cnt && !fun_chip_ready(mtd))
			cpu_relax();
		if (!cnt)
			dev_err(fun->dev, "tired waiting for RNB\n");
	} else {
		ndelay(100);
	}
}

static void fun_cmd_ctrl(struct mtd_info *mtd, int cmd, unsigned int ctrl)
{
	struct nand_chip *chip = mtd_to_nand(mtd);
	struct fsl_upm_nand *fun = to_fsl_upm_nand(mtd);
	u32 mar;

	if (!(ctrl & fun->last_ctrl)) {
		fsl_upm_end_pattern(&fun->upm);

		if (cmd == NAND_CMD_NONE)
			return;

		fun->last_ctrl = ctrl & (NAND_ALE | NAND_CLE);
	}

	if (ctrl & NAND_CTRL_CHANGE) {
		if (ctrl & NAND_ALE)
			fsl_upm_start_pattern(&fun->upm, fun->upm_addr_offset);
		else if (ctrl & NAND_CLE)
			fsl_upm_start_pattern(&fun->upm, fun->upm_cmd_offset);
	}

	mar = (cmd << (32 - fun->upm.width)) |
		fun->mchip_offsets[fun->mchip_number];
	fsl_upm_run_pattern(&fun->upm, chip->IO_ADDR_R, mar);

	if (fun->wait_flags & FSL_UPM_WAIT_RUN_PATTERN)
		fun_wait_rnb(fun);
}

static void fun_select_chip(struct mtd_info *mtd, int mchip_nr)
{
	struct nand_chip *chip = mtd_to_nand(mtd);
	struct fsl_upm_nand *fun = to_fsl_upm_nand(mtd);

	if (mchip_nr == -1) {
		chip->cmd_ctrl(mtd, NAND_CMD_NONE, 0 | NAND_CTRL_CHANGE);
	} else if (mchip_nr >= 0 && mchip_nr < NAND_MAX_CHIPS) {
		fun->mchip_number = mchip_nr;
		chip->IO_ADDR_R = fun->io_base + fun->mchip_offsets[mchip_nr];
		chip->IO_ADDR_W = chip->IO_ADDR_R;
	} else {
		BUG();
	}
}

static uint8_t fun_read_byte(struct mtd_info *mtd)
{
	struct fsl_upm_nand *fun = to_fsl_upm_nand(mtd);

	return in_8(fun->chip.IO_ADDR_R);
}

static void fun_read_buf(struct mtd_info *mtd, uint8_t *buf, int len)
{
	struct fsl_upm_nand *fun = to_fsl_upm_nand(mtd);
	int i;

	for (i = 0; i < len; i++)
		buf[i] = in_8(fun->chip.IO_ADDR_R);
}

static void fun_write_buf(struct mtd_info *mtd, const uint8_t *buf, int len)
{
	struct fsl_upm_nand *fun = to_fsl_upm_nand(mtd);
	int i;

	for (i = 0; i < len; i++) {
		out_8(fun->chip.IO_ADDR_W, buf[i]);
		if (fun->wait_flags & FSL_UPM_WAIT_WRITE_BYTE)
			fun_wait_rnb(fun);
	}
	if (fun->wait_flags & FSL_UPM_WAIT_WRITE_BUFFER)
		fun_wait_rnb(fun);
}

static int fun_chip_init(struct fsl_upm_nand *fun,
			 const struct device_node *upm_np,
			 const struct resource *io_res)
{
	struct mtd_info *mtd = nand_to_mtd(&fun->chip);
	int ret;
	struct device_node *flash_np;

	fun->chip.IO_ADDR_R = fun->io_base;
	fun->chip.IO_ADDR_W = fun->io_base;
	fun->chip.cmd_ctrl = fun_cmd_ctrl;
	fun->chip.chip_delay = fun->chip_delay;
	fun->chip.read_byte = fun_read_byte;
	fun->chip.read_buf = fun_read_buf;
	fun->chip.write_buf = fun_write_buf;
	fun->chip.ecc.mode = NAND_ECC_SOFT;
	if (fun->mchip_count > 1)
		fun->chip.select_chip = fun_select_chip;

	if (fun->rnb_gpio[0] >= 0)
		fun->chip.dev_ready = fun_chip_ready;

	mtd->dev.parent = fun->dev;

	flash_np = of_get_next_child(upm_np, NULL);
	if (!flash_np)
		return -ENODEV;

	nand_set_flash_node(&fun->chip, flash_np);
	mtd->name = kasprintf(GFP_KERNEL, "0x%llx.%s", (u64)io_res->start,
			      flash_np->name);
	if (!mtd->name) {
		ret = -ENOMEM;
		goto err;
	}

	ret = nand_scan(mtd, fun->mchip_count);
	if (ret)
		goto err;

	ret = mtd_device_register(mtd, NULL, 0);
err:
	of_node_put(flash_np);
	if (ret)
		kfree(mtd->name);
	return ret;
}

static int fun_probe(struct platform_device *ofdev)
{
	struct fsl_upm_nand *fun;
	struct resource io_res;
	const __be32 *prop;
	int rnb_gpio;
	int ret;
	int size;
	int i;

	fun = kzalloc(sizeof(*fun), GFP_KERNEL);
	if (!fun)
		return -ENOMEM;

	ret = of_address_to_resource(ofdev->dev.of_node, 0, &io_res);
	if (ret) {
		dev_err(&ofdev->dev, "can't get IO base\n");
		goto err1;
	}

	ret = fsl_upm_find(io_res.start, &fun->upm);
	if (ret) {
		dev_err(&ofdev->dev, "can't find UPM\n");
		goto err1;
	}

	prop = of_get_property(ofdev->dev.of_node, "fsl,upm-addr-offset",
			       &size);
	if (!prop || size != sizeof(uint32_t)) {
		dev_err(&ofdev->dev, "can't get UPM address offset\n");
		ret = -EINVAL;
		goto err1;
	}
	fun->upm_addr_offset = *prop;

	prop = of_get_property(ofdev->dev.of_node, "fsl,upm-cmd-offset", &size);
	if (!prop || size != sizeof(uint32_t)) {
		dev_err(&ofdev->dev, "can't get UPM command offset\n");
		ret = -EINVAL;
		goto err1;
	}
	fun->upm_cmd_offset = *prop;

	prop = of_get_property(ofdev->dev.of_node,
			       "fsl,upm-addr-line-cs-offsets", &size);
	if (prop && (size / sizeof(uint32_t)) > 0) {
		fun->mchip_count = size / sizeof(uint32_t);
		if (fun->mchip_count >= NAND_MAX_CHIPS) {
			dev_err(&ofdev->dev, "too much multiple chips\n");
			goto err1;
		}
		for (i = 0; i < fun->mchip_count; i++)
			fun->mchip_offsets[i] = be32_to_cpu(prop[i]);
	} else {
		fun->mchip_count = 1;
	}

	for (i = 0; i < fun->mchip_count; i++) {
		fun->rnb_gpio[i] = -1;
		rnb_gpio = of_get_gpio(ofdev->dev.of_node, i);
		if (rnb_gpio >= 0) {
			ret = gpio_request(rnb_gpio, dev_name(&ofdev->dev));
			if (ret) {
				dev_err(&ofdev->dev,
					"can't request RNB gpio #%d\n", i);
				goto err2;
			}
			gpio_direction_input(rnb_gpio);
			fun->rnb_gpio[i] = rnb_gpio;
		} else if (rnb_gpio == -EINVAL) {
			dev_err(&ofdev->dev, "RNB gpio #%d is invalid\n", i);
			goto err2;
		}
	}

	prop = of_get_property(ofdev->dev.of_node, "chip-delay", NULL);
	if (prop)
		fun->chip_delay = be32_to_cpup(prop);
	else
		fun->chip_delay = 50;

	prop = of_get_property(ofdev->dev.of_node, "fsl,upm-wait-flags", &size);
	if (prop && size == sizeof(uint32_t))
		fun->wait_flags = be32_to_cpup(prop);
	else
		fun->wait_flags = FSL_UPM_WAIT_RUN_PATTERN |
				  FSL_UPM_WAIT_WRITE_BYTE;

	fun->io_base = devm_ioremap_nocache(&ofdev->dev, io_res.start,
					    resource_size(&io_res));
	if (!fun->io_base) {
		ret = -ENOMEM;
		goto err2;
	}

	fun->dev = &ofdev->dev;
	fun->last_ctrl = NAND_CLE;

	ret = fun_chip_init(fun, ofdev->dev.of_node, &io_res);
	if (ret)
		goto err2;

	dev_set_drvdata(&ofdev->dev, fun);

	return 0;
err2:
	for (i = 0; i < fun->mchip_count; i++) {
		if (fun->rnb_gpio[i] < 0)
			break;
		gpio_free(fun->rnb_gpio[i]);
	}
err1:
	kfree(fun);

	return ret;
}

static int fun_remove(struct platform_device *ofdev)
{
	struct fsl_upm_nand *fun = dev_get_drvdata(&ofdev->dev);
	struct mtd_info *mtd = nand_to_mtd(&fun->chip);
	int i;

	nand_release(mtd);
	kfree(mtd->name);

	for (i = 0; i < fun->mchip_count; i++) {
		if (fun->rnb_gpio[i] < 0)
			break;
		gpio_free(fun->rnb_gpio[i]);
	}

	kfree(fun);

	return 0;
}

static const struct of_device_id of_fun_match[] = {
	{ .compatible = "fsl,upm-nand" },
	{},
};
MODULE_DEVICE_TABLE(of, of_fun_match);

static struct platform_driver of_fun_driver = {
	.driver = {
		.name = "fsl,upm-nand",
		.of_match_table = of_fun_match,
	},
	.probe		= fun_probe,
	.remove		= fun_remove,
};

module_platform_driver(of_fun_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Anton Vorontsov <avorontsov@ru.mvista.com>");
MODULE_DESCRIPTION("Driver for NAND chips working through Freescale "
		   "LocalBus User-Programmable Machine");
