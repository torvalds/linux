/*
 * Generic NAND driver
 *
 * Author: Vitaly Wool <vitalywool@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */

#include <linux/err.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/mtd/mtd.h>
#include <linux/mtd/platnand.h>

struct plat_nand_data {
	struct nand_chip	chip;
	void __iomem		*io_base;
};

/*
 * Probe for the NAND device.
 */
static int plat_nand_probe(struct platform_device *pdev)
{
	struct platform_nand_data *pdata = dev_get_platdata(&pdev->dev);
	struct plat_nand_data *data;
	struct mtd_info *mtd;
	struct resource *res;
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

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	data->io_base = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(data->io_base))
		return PTR_ERR(data->io_base);

	nand_set_flash_node(&data->chip, pdev->dev.of_node);
	mtd = nand_to_mtd(&data->chip);
	mtd->dev.parent = &pdev->dev;

	data->chip.legacy.IO_ADDR_R = data->io_base;
	data->chip.legacy.IO_ADDR_W = data->io_base;
	data->chip.legacy.cmd_ctrl = pdata->ctrl.cmd_ctrl;
	data->chip.legacy.dev_ready = pdata->ctrl.dev_ready;
	data->chip.select_chip = pdata->ctrl.select_chip;
	data->chip.legacy.write_buf = pdata->ctrl.write_buf;
	data->chip.legacy.read_buf = pdata->ctrl.read_buf;
	data->chip.legacy.chip_delay = pdata->chip.chip_delay;
	data->chip.options |= pdata->chip.options;
	data->chip.bbt_options |= pdata->chip.bbt_options;

	data->chip.ecc.mode = NAND_ECC_SOFT;
	data->chip.ecc.algo = NAND_ECC_HAMMING;

	platform_set_drvdata(pdev, data);

	/* Handle any platform specific setup */
	if (pdata->ctrl.probe) {
		err = pdata->ctrl.probe(pdev);
		if (err)
			goto out;
	}

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

	nand_release(&data->chip);
out:
	if (pdata->ctrl.remove)
		pdata->ctrl.remove(pdev);
	return err;
}

/*
 * Remove a NAND device.
 */
static int plat_nand_remove(struct platform_device *pdev)
{
	struct plat_nand_data *data = platform_get_drvdata(pdev);
	struct platform_nand_data *pdata = dev_get_platdata(&pdev->dev);

	nand_release(&data->chip);
	if (pdata->ctrl.remove)
		pdata->ctrl.remove(pdev);

	return 0;
}

static const struct of_device_id plat_nand_match[] = {
	{ .compatible = "gen_nand" },
	{},
};
MODULE_DEVICE_TABLE(of, plat_nand_match);

static struct platform_driver plat_nand_driver = {
	.probe	= plat_nand_probe,
	.remove	= plat_nand_remove,
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
