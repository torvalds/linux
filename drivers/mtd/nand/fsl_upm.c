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
#include <linux/mtd/nand.h>
#include <linux/mtd/nand_ecc.h>
#include <linux/mtd/partitions.h>
#include <linux/mtd/mtd.h>
#include <linux/of_platform.h>
#include <linux/of_gpio.h>
#include <linux/io.h>
#include <asm/fsl_lbc.h>

struct fsl_upm_nand {
	struct device *dev;
	struct mtd_info mtd;
	struct nand_chip chip;
	int last_ctrl;
#ifdef CONFIG_MTD_PARTITIONS
	struct mtd_partition *parts;
#endif

	struct fsl_upm upm;
	uint8_t upm_addr_offset;
	uint8_t upm_cmd_offset;
	void __iomem *io_base;
	int rnb_gpio;
};

#define to_fsl_upm_nand(mtd) container_of(mtd, struct fsl_upm_nand, mtd)

static int fun_chip_ready(struct mtd_info *mtd)
{
	struct fsl_upm_nand *fun = to_fsl_upm_nand(mtd);

	if (gpio_get_value(fun->rnb_gpio))
		return 1;

	dev_vdbg(fun->dev, "busy\n");
	return 0;
}

static void fun_wait_rnb(struct fsl_upm_nand *fun)
{
	int cnt = 1000000;

	if (fun->rnb_gpio >= 0) {
		while (--cnt && !fun_chip_ready(&fun->mtd))
			cpu_relax();
	}

	if (!cnt)
		dev_err(fun->dev, "tired waiting for RNB\n");
}

static void fun_cmd_ctrl(struct mtd_info *mtd, int cmd, unsigned int ctrl)
{
	struct fsl_upm_nand *fun = to_fsl_upm_nand(mtd);

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

	fsl_upm_run_pattern(&fun->upm, fun->io_base, cmd);

	fun_wait_rnb(fun);
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
		fun_wait_rnb(fun);
	}
}

static int __devinit fun_chip_init(struct fsl_upm_nand *fun,
				   const struct device_node *upm_np,
				   const struct resource *io_res)
{
	int ret;
	struct device_node *flash_np;
#ifdef CONFIG_MTD_PARTITIONS
	static const char *part_types[] = { "cmdlinepart", NULL, };
#endif

	fun->chip.IO_ADDR_R = fun->io_base;
	fun->chip.IO_ADDR_W = fun->io_base;
	fun->chip.cmd_ctrl = fun_cmd_ctrl;
	fun->chip.chip_delay = 50;
	fun->chip.read_byte = fun_read_byte;
	fun->chip.read_buf = fun_read_buf;
	fun->chip.write_buf = fun_write_buf;
	fun->chip.ecc.mode = NAND_ECC_SOFT;

	if (fun->rnb_gpio >= 0)
		fun->chip.dev_ready = fun_chip_ready;

	fun->mtd.priv = &fun->chip;
	fun->mtd.owner = THIS_MODULE;

	flash_np = of_get_next_child(upm_np, NULL);
	if (!flash_np)
		return -ENODEV;

	fun->mtd.name = kasprintf(GFP_KERNEL, "%x.%s", io_res->start,
				  flash_np->name);
	if (!fun->mtd.name) {
		ret = -ENOMEM;
		goto err;
	}

	ret = nand_scan(&fun->mtd, 1);
	if (ret)
		goto err;

#ifdef CONFIG_MTD_PARTITIONS
	ret = parse_mtd_partitions(&fun->mtd, part_types, &fun->parts, 0);

#ifdef CONFIG_MTD_OF_PARTS
	if (ret == 0)
		ret = of_mtd_parse_partitions(fun->dev, &fun->mtd,
					      flash_np, &fun->parts);
#endif
	if (ret > 0)
		ret = add_mtd_partitions(&fun->mtd, fun->parts, ret);
	else
#endif
		ret = add_mtd_device(&fun->mtd);
err:
	of_node_put(flash_np);
	return ret;
}

static int __devinit fun_probe(struct of_device *ofdev,
			       const struct of_device_id *ofid)
{
	struct fsl_upm_nand *fun;
	struct resource io_res;
	const uint32_t *prop;
	int ret;
	int size;

	fun = kzalloc(sizeof(*fun), GFP_KERNEL);
	if (!fun)
		return -ENOMEM;

	ret = of_address_to_resource(ofdev->node, 0, &io_res);
	if (ret) {
		dev_err(&ofdev->dev, "can't get IO base\n");
		goto err1;
	}

	ret = fsl_upm_find(io_res.start, &fun->upm);
	if (ret) {
		dev_err(&ofdev->dev, "can't find UPM\n");
		goto err1;
	}

	prop = of_get_property(ofdev->node, "fsl,upm-addr-offset", &size);
	if (!prop || size != sizeof(uint32_t)) {
		dev_err(&ofdev->dev, "can't get UPM address offset\n");
		ret = -EINVAL;
		goto err2;
	}
	fun->upm_addr_offset = *prop;

	prop = of_get_property(ofdev->node, "fsl,upm-cmd-offset", &size);
	if (!prop || size != sizeof(uint32_t)) {
		dev_err(&ofdev->dev, "can't get UPM command offset\n");
		ret = -EINVAL;
		goto err2;
	}
	fun->upm_cmd_offset = *prop;

	fun->rnb_gpio = of_get_gpio(ofdev->node, 0);
	if (fun->rnb_gpio >= 0) {
		ret = gpio_request(fun->rnb_gpio, ofdev->dev.bus_id);
		if (ret) {
			dev_err(&ofdev->dev, "can't request RNB gpio\n");
			goto err2;
		}
		gpio_direction_input(fun->rnb_gpio);
	} else if (fun->rnb_gpio == -EINVAL) {
		dev_err(&ofdev->dev, "specified RNB gpio is invalid\n");
		goto err2;
	}

	fun->io_base = devm_ioremap_nocache(&ofdev->dev, io_res.start,
					  io_res.end - io_res.start + 1);
	if (!fun->io_base) {
		ret = -ENOMEM;
		goto err2;
	}

	fun->dev = &ofdev->dev;
	fun->last_ctrl = NAND_CLE;

	ret = fun_chip_init(fun, ofdev->node, &io_res);
	if (ret)
		goto err2;

	dev_set_drvdata(&ofdev->dev, fun);

	return 0;
err2:
	if (fun->rnb_gpio >= 0)
		gpio_free(fun->rnb_gpio);
err1:
	kfree(fun);

	return ret;
}

static int __devexit fun_remove(struct of_device *ofdev)
{
	struct fsl_upm_nand *fun = dev_get_drvdata(&ofdev->dev);

	nand_release(&fun->mtd);
	kfree(fun->mtd.name);

	if (fun->rnb_gpio >= 0)
		gpio_free(fun->rnb_gpio);

	kfree(fun);

	return 0;
}

static struct of_device_id of_fun_match[] = {
	{ .compatible = "fsl,upm-nand" },
	{},
};
MODULE_DEVICE_TABLE(of, of_fun_match);

static struct of_platform_driver of_fun_driver = {
	.name		= "fsl,upm-nand",
	.match_table	= of_fun_match,
	.probe		= fun_probe,
	.remove		= __devexit_p(fun_remove),
};

static int __init fun_module_init(void)
{
	return of_register_platform_driver(&of_fun_driver);
}
module_init(fun_module_init);

static void __exit fun_module_exit(void)
{
	of_unregister_platform_driver(&of_fun_driver);
}
module_exit(fun_module_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Anton Vorontsov <avorontsov@ru.mvista.com>");
MODULE_DESCRIPTION("Driver for NAND chips working through Freescale "
		   "LocalBus User-Programmable Machine");
