// SPDX-License-Identifier: GPL-2.0-only
/*
 * Generic NAND driver
 *
 * Author: Vitaly Wool <vitalywool@gmail.com>
 */

#include <linux/err.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/mtd/mtd.h>
#include <linux/mtd/platnand.h>

struct plat_nand_data {
	struct nand_controller	controller;
	struct nand_chip	chip;
	void __iomem		*io_base;
};

static int plat_nand_attach_chip(struct nand_chip *chip)
{
	if (chip->ecc.engine_type == NAND_ECC_ENGINE_TYPE_SOFT &&
	    chip->ecc.algo == NAND_ECC_ALGO_UNKNOWN)
		chip->ecc.algo = NAND_ECC_ALGO_HAMMING;

	return 0;
}

static const struct nand_controller_ops plat_nand_ops = {
	.attach_chip = plat_nand_attach_chip,
};

/*
 * Probe for the NAND device.
 */
static int plat_nand_probe(struct platform_device *pdev)
{
	struct platform_nand_data *pdata = dev_get_platdata(&pdev->dev);
	struct plat_nand_data *data;
	struct mtd_info *mtd;
	const char **part_types;
	int err = 0;

	if (!pdata) {
		dev_err(&pdev->dev, "platform_nand_data is missing\n");
		return -EINVAL;
	}

	if (pdata->chip.nr_chips < 1) {
		dev_err(&pdev->dev, "invalid number of chips specified\n");
		return -EINVAL;
	}

	/* Allocate memory for the device structure (and zero it) */
	data = devm_kzalloc(&pdev->dev, sizeof(struct plat_nand_data),
			    GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	data->controller.ops = &plat_nand_ops;
	nand_controller_init(&data->controller);
	data->chip.controller = &data->controller;

	data->io_base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(data->io_base))
		return PTR_ERR(data->io_base);

	nand_set_flash_node(&data->chip, pdev->dev.of_node);
	mtd = nand_to_mtd(&data->chip);
	mtd->dev.parent = &pdev->dev;

	data->chip.legacy.IO_ADDR_R = data->io_base;
	data->chip.legacy.IO_ADDR_W = data->io_base;
	data->chip.legacy.cmd_ctrl = pdata->ctrl.cmd_ctrl;
	data->chip.legacy.dev_ready = pdata->ctrl.dev_ready;
	data->chip.legacy.select_chip = pdata->ctrl.select_chip;
	data->chip.legacy.write_buf = pdata->ctrl.write_buf;
	data->chip.legacy.read_buf = pdata->ctrl.read_buf;
	data->chip.legacy.chip_delay = pdata->chip.chip_delay;
	data->chip.options |= pdata->chip.options;
	data->chip.bbt_options |= pdata->chip.bbt_options;

	platform_set_drvdata(pdev, data);

	/* Handle any platform specific setup */
	if (pdata->ctrl.probe) {
		err = pdata->ctrl.probe(pdev);
		if (err)
			goto out;
	}

	/*
	 * This driver assumes that the default ECC engine should be TYPE_SOFT.
	 * Set ->engine_type before registering the NAND devices in order to
	 * provide a driver specific default value.
	 */
	data->chip.ecc.engine_type = NAND_ECC_ENGINE_TYPE_SOFT;

	/* Scan to find existence of the device */
	err = nand_scan(&data->chip, pdata->chip.nr_chips);
	if (err)
		goto out;

	part_types = pdata->chip.part_probe_types;

	err = mtd_device_parse_register(mtd, part_types, NULL,
					pdata->chip.partitions,
					pdata->chip.nr_partitions);

	if (!err)
		return err;

	nand_cleanup(&data->chip);
out:
	if (pdata->ctrl.remove)
		pdata->ctrl.remove(pdev);
	return err;
}

/*
 * Remove a NAND device.
 */
static void plat_nand_remove(struct platform_device *pdev)
{
	struct plat_nand_data *data = platform_get_drvdata(pdev);
	struct platform_nand_data *pdata = dev_get_platdata(&pdev->dev);
	struct nand_chip *chip = &data->chip;
	int ret;

	ret = mtd_device_unregister(nand_to_mtd(chip));
	WARN_ON(ret);
	nand_cleanup(chip);
	if (pdata->ctrl.remove)
		pdata->ctrl.remove(pdev);
}

static const struct of_device_id plat_nand_match[] = {
	{ .compatible = "gen_nand" },
	{},
};
MODULE_DEVICE_TABLE(of, plat_nand_match);

static struct platform_driver plat_nand_driver = {
	.probe	= plat_nand_probe,
	.remove = plat_nand_remove,
	.driver	= {
		.name		= "gen_nand",
		.of_match_table = plat_nand_match,
	},
};

module_platform_driver(plat_nand_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Vitaly Wool");
MODULE_DESCRIPTION("Simple generic NAND driver");
MODULE_ALIAS("platform:gen_nand");
