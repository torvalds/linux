/*
 * NAND Flash Controller Device Driver for DT
 *
 * Copyright © 2011, Picochip.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 */
#include <linux/clk.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/ioport.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/slab.h>

#include "denali.h"

struct denali_dt {
	struct denali_nand_info	denali;
	struct clk		*clk;
};

static void __iomem *request_and_map(struct device *dev,
				     const struct resource *res)
{
	void __iomem *ptr;

	if (!devm_request_mem_region(dev, res->start, resource_size(res),
				     "denali-dt")) {
		dev_err(dev, "unable to request %s\n", res->name);
		return NULL;
	}

	ptr = devm_ioremap_nocache(dev, res->start, resource_size(res));
	if (!res)
		dev_err(dev, "ioremap_nocache of %s failed!", res->name);

	return ptr;
}

static const struct of_device_id denali_nand_dt_ids[] = {
		{ .compatible = "denali,denali-nand-dt" },
		{ /* sentinel */ }
	};

MODULE_DEVICE_TABLE(of, denali_nand_dt_ids);

static u64 denali_dma_mask;

static int denali_dt_probe(struct platform_device *ofdev)
{
	struct resource *denali_reg, *nand_data;
	struct denali_dt *dt;
	struct denali_nand_info *denali;
	int ret;
	const struct of_device_id *of_id;

	of_id = of_match_device(denali_nand_dt_ids, &ofdev->dev);
	if (of_id) {
		ofdev->id_entry = of_id->data;
	} else {
		pr_err("Failed to find the right device id.\n");
		return -ENOMEM;
	}

	dt = devm_kzalloc(&ofdev->dev, sizeof(*dt), GFP_KERNEL);
	if (!dt)
		return -ENOMEM;
	denali = &dt->denali;

	denali_reg = platform_get_resource_byname(ofdev, IORESOURCE_MEM, "denali_reg");
	nand_data = platform_get_resource_byname(ofdev, IORESOURCE_MEM, "nand_data");
	if (!denali_reg || !nand_data) {
		dev_err(&ofdev->dev, "resources not completely defined\n");
		return -EINVAL;
	}

	denali->platform = DT;
	denali->dev = &ofdev->dev;
	denali->irq = platform_get_irq(ofdev, 0);
	if (denali->irq < 0) {
		dev_err(&ofdev->dev, "no irq defined\n");
		return -ENXIO;
	}

	denali->flash_reg = request_and_map(&ofdev->dev, denali_reg);
	if (!denali->flash_reg)
		return -ENOMEM;

	denali->flash_mem = request_and_map(&ofdev->dev, nand_data);
	if (!denali->flash_mem)
		return -ENOMEM;

	if (!of_property_read_u32(ofdev->dev.of_node,
		"dma-mask", (u32 *)&denali_dma_mask)) {
		denali->dev->dma_mask = &denali_dma_mask;
	} else {
		denali->dev->dma_mask = NULL;
	}

	dt->clk = clk_get(&ofdev->dev, NULL);
	if (IS_ERR(dt->clk)) {
		dev_err(&ofdev->dev, "no clk available\n");
		return PTR_ERR(dt->clk);
	}
	clk_prepare_enable(dt->clk);

	ret = denali_init(denali);
	if (ret)
		goto out_disable_clk;

	platform_set_drvdata(ofdev, dt);
	return 0;

out_disable_clk:
	clk_disable_unprepare(dt->clk);
	clk_put(dt->clk);

	return ret;
}

static int __devexit denali_dt_remove(struct platform_device *ofdev)
{
	struct denali_dt *dt = platform_get_drvdata(ofdev);

	denali_remove(&dt->denali);
	clk_disable(dt->clk);
	clk_put(dt->clk);

	return 0;
}

static struct platform_driver denali_dt_driver = {
	.probe		= denali_dt_probe,
	.remove		= denali_dt_remove,
	.driver		= {
		.name	= "denali-nand-dt",
		.owner	= THIS_MODULE,
		.of_match_table	= of_match_ptr(denali_nand_dt_ids),
	},
};

static int __init denali_init_dt(void)
{
	return platform_driver_register(&denali_dt_driver);
}
module_init(denali_init_dt);

static void __exit denali_exit_dt(void)
{
	platform_driver_unregister(&denali_dt_driver);
}
module_exit(denali_exit_dt);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Jamie Iles");
MODULE_DESCRIPTION("DT driver for Denali NAND controller");
