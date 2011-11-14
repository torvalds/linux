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
};

/*
 * Probe for the NAND device.
 */
static int __devinit plat_nand_probe(struct platform_device *pdev)
{
	struct platform_nand_data *pdata = pdev->dev.platform_data;
	struct plat_nand_data *data;
	struct resource *res;
	int err = 0;

	if (pdata->chip.nr_chips < 1) {
		dev_err(&pdev->dev, "invalid number of chips specified\n");
		return -EINVAL;
	}

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res)
		return -ENXIO;

	/* Allocate memory for the device structure (and zero it) */
	data = kzalloc(sizeof(struct plat_nand_data), GFP_KERNEL);
	if (!data) {
		dev_err(&pdev->dev, "failed to allocate device structure.\n");
		return -ENOMEM;
	}

	if (!request_mem_region(res->start, resource_size(res),
				dev_name(&pdev->dev))) {
		dev_err(&pdev->dev, "request_mem_region failed\n");
		err = -EBUSY;
		goto out_free;
	}

	data->io_base = ioremap(res->start, resource_size(res));
	if (data->io_base == NULL) {
		dev_err(&pdev->dev, "ioremap failed\n");
		err = -EIO;
		goto out_release_io;
	}

	data->chip.priv = &data;
	data->mtd.priv = &data->chip;
	data->mtd.owner = THIS_MODULE;
	data->mtd.name = dev_name(&pdev->dev);

	data->chip.IO_ADDR_R = data->io_base;
	data->chip.IO_ADDR_W = data->io_base;
	data->chip.cmd_ctrl = pdata->ctrl.cmd_ctrl;
	data->chip.dev_ready = pdata->ctrl.dev_ready;
	data->chip.select_chip = pdata->ctrl.select_chip;
	data->chip.write_buf = pdata->ctrl.write_buf;
	data->chip.read_buf = pdata->ctrl.read_buf;
	data->chip.chip_delay = pdata->chip.chip_delay;
	data->chip.options |= pdata->chip.options;
	data->chip.bbt_options |= pdata->chip.bbt_options;

	data->chip.ecc.hwctl = pdata->ctrl.hwcontrol;
	data->chip.ecc.layout = pdata->chip.ecclayout;
	data->chip.ecc.mode = NAND_ECC_SOFT;

	platform_set_drvdata(pdev, data);

	/* Handle any platform specific setup */
	if (pdata->ctrl.probe) {
		err = pdata->ctrl.probe(pdev);
		if (err)
			goto out;
	}

	/* Scan to find existence of the device */
	if (nand_scan(&data->mtd, pdata->chip.nr_chips)) {
		err = -ENXIO;
		goto out;
	}

	err = mtd_device_parse_register(&data->mtd,
			pdata->chip.part_probe_types, 0,
			pdata->chip.partitions, pdata->chip.nr_partitions);

	if (!err)
		return err;

	nand_release(&data->mtd);
out:
	if (pdata->ctrl.remove)
		pdata->ctrl.remove(pdev);
	platform_set_drvdata(pdev, NULL);
	iounmap(data->io_base);
out_release_io:
	release_mem_region(res->start, resource_size(res));
out_free:
	kfree(data);
	return err;
}

/*
 * Remove a NAND device.
 */
static int __devexit plat_nand_remove(struct platform_device *pdev)
{
	struct plat_nand_data *data = platform_get_drvdata(pdev);
	struct platform_nand_data *pdata = pdev->dev.platform_data;
	struct resource *res;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);

	nand_release(&data->mtd);
	if (pdata->ctrl.remove)
		pdata->ctrl.remove(pdev);
	iounmap(data->io_base);
	release_mem_region(res->start, resource_size(res));
	kfree(data);

	return 0;
}

static struct platform_driver plat_nand_driver = {
	.probe		= plat_nand_probe,
	.remove		= __devexit_p(plat_nand_remove),
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
MODULE_ALIAS("platform:gen_nand");
