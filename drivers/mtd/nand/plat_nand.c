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

#include <linux/io.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/mtd/mtd.h>
#include <linux/mtd/nand.h>
#include <linux/mtd/partitions.h>

struct plat_nand_data {
	struct nand_chip	chip;
	struct mtd_info		mtd;
	void __iomem		*io_base;
#ifdef CONFIG_MTD_PARTITIONS
	int			nr_parts;
	struct mtd_partition	*parts;
#endif
};

/*
 * Probe for the NAND device.
 */
static int __init plat_nand_probe(struct platform_device *pdev)
{
	struct platform_nand_data *pdata = pdev->dev.platform_data;
	struct plat_nand_data *data;
	int res = 0;

	/* Allocate memory for the device structure (and zero it) */
	data = kzalloc(sizeof(struct plat_nand_data), GFP_KERNEL);
	if (!data) {
		dev_err(&pdev->dev, "failed to allocate device structure.\n");
		return -ENOMEM;
	}

	data->io_base = ioremap(pdev->resource[0].start,
				pdev->resource[0].end - pdev->resource[0].start + 1);
	if (data->io_base == NULL) {
		dev_err(&pdev->dev, "ioremap failed\n");
		kfree(data);
		return -EIO;
	}

	data->chip.priv = &data;
	data->mtd.priv = &data->chip;
	data->mtd.owner = THIS_MODULE;

	data->chip.IO_ADDR_R = data->io_base;
	data->chip.IO_ADDR_W = data->io_base;
	data->chip.cmd_ctrl = pdata->ctrl.cmd_ctrl;
	data->chip.dev_ready = pdata->ctrl.dev_ready;
	data->chip.select_chip = pdata->ctrl.select_chip;
	data->chip.chip_delay = pdata->chip.chip_delay;
	data->chip.options |= pdata->chip.options;

	data->chip.ecc.hwctl = pdata->ctrl.hwcontrol;
	data->chip.ecc.layout = pdata->chip.ecclayout;
	data->chip.ecc.mode = NAND_ECC_SOFT;

	platform_set_drvdata(pdev, data);

	/* Scan to find existance of the device */
	if (nand_scan(&data->mtd, 1)) {
		res = -ENXIO;
		goto out;
	}

#ifdef CONFIG_MTD_PARTITIONS
	if (pdata->chip.part_probe_types) {
		res = parse_mtd_partitions(&data->mtd,
					pdata->chip.part_probe_types,
					&data->parts, 0);
		if (res > 0) {
			add_mtd_partitions(&data->mtd, data->parts, res);
			return 0;
		}
	}
	if (pdata->chip.partitions) {
		data->parts = pdata->chip.partitions;
		res = add_mtd_partitions(&data->mtd, data->parts,
			pdata->chip.nr_partitions);
	} else
#endif
	res = add_mtd_device(&data->mtd);

	if (!res)
		return res;

	nand_release(&data->mtd);
out:
	platform_set_drvdata(pdev, NULL);
	iounmap(data->io_base);
	kfree(data);
	return res;
}

/*
 * Remove a NAND device.
 */
static int __devexit plat_nand_remove(struct platform_device *pdev)
{
	struct plat_nand_data *data = platform_get_drvdata(pdev);
	struct platform_nand_data *pdata = pdev->dev.platform_data;

	nand_release(&data->mtd);
#ifdef CONFIG_MTD_PARTITIONS
	if (data->parts && data->parts != pdata->chip.partitions)
		kfree(data->parts);
#endif
	iounmap(data->io_base);
	kfree(data);

	return 0;
}

static struct platform_driver plat_nand_driver = {
	.probe		= plat_nand_probe,
	.remove		= plat_nand_remove,
	.driver		= {
		.name	= "gen_nand",
		.owner	= THIS_MODULE,
	},
};

static int __init plat_nand_init(void)
{
	return platform_driver_register(&plat_nand_driver);
}

static void __exit plat_nand_exit(void)
{
	platform_driver_unregister(&plat_nand_driver);
}

module_init(plat_nand_init);
module_exit(plat_nand_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Vitaly Wool");
MODULE_DESCRIPTION("Simple generic NAND driver");
