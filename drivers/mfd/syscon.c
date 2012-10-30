/*
 * System Control Driver
 *
 * Copyright (C) 2012 Freescale Semiconductor, Inc.
 * Copyright (C) 2012 Linaro Ltd.
 *
 * Author: Dong Aisheng <dong.aisheng@linaro.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <linux/err.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>

static struct platform_driver syscon_driver;

struct syscon {
	struct device *dev;
	void __iomem *base;
	struct regmap *regmap;
};

static int syscon_match(struct device *dev, void *data)
{
	struct syscon *syscon = dev_get_drvdata(dev);
	struct device_node *dn = data;

	return (syscon->dev->of_node == dn) ? 1 : 0;
}

struct regmap *syscon_node_to_regmap(struct device_node *np)
{
	struct syscon *syscon;
	struct device *dev;

	dev = driver_find_device(&syscon_driver.driver, NULL, np,
				 syscon_match);
	if (!dev)
		return ERR_PTR(-EPROBE_DEFER);

	syscon = dev_get_drvdata(dev);

	return syscon->regmap;
}
EXPORT_SYMBOL_GPL(syscon_node_to_regmap);

struct regmap *syscon_regmap_lookup_by_compatible(const char *s)
{
	struct device_node *syscon_np;
	struct regmap *regmap;

	syscon_np = of_find_compatible_node(NULL, NULL, s);
	if (!syscon_np)
		return ERR_PTR(-ENODEV);

	regmap = syscon_node_to_regmap(syscon_np);
	of_node_put(syscon_np);

	return regmap;
}
EXPORT_SYMBOL_GPL(syscon_regmap_lookup_by_compatible);

struct regmap *syscon_regmap_lookup_by_phandle(struct device_node *np,
					const char *property)
{
	struct device_node *syscon_np;
	struct regmap *regmap;

	syscon_np = of_parse_phandle(np, property, 0);
	if (!syscon_np)
		return ERR_PTR(-ENODEV);

	regmap = syscon_node_to_regmap(syscon_np);
	of_node_put(syscon_np);

	return regmap;
}
EXPORT_SYMBOL_GPL(syscon_regmap_lookup_by_phandle);

static const struct of_device_id of_syscon_match[] = {
	{ .compatible = "syscon", },
	{ },
};

static struct regmap_config syscon_regmap_config = {
	.reg_bits = 32,
	.val_bits = 32,
	.reg_stride = 4,
};

static int __devinit syscon_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct device_node *np = dev->of_node;
	struct syscon *syscon;
	struct resource res;
	int ret;

	if (!np)
		return -ENOENT;

	syscon = devm_kzalloc(dev, sizeof(struct syscon),
			    GFP_KERNEL);
	if (!syscon)
		return -ENOMEM;

	syscon->base = of_iomap(np, 0);
	if (!syscon->base)
		return -EADDRNOTAVAIL;

	ret = of_address_to_resource(np, 0, &res);
	if (ret)
		return ret;

	syscon_regmap_config.max_register = res.end - res.start - 3;
	syscon->regmap = devm_regmap_init_mmio(dev, syscon->base,
					&syscon_regmap_config);
	if (IS_ERR(syscon->regmap)) {
		dev_err(dev, "regmap init failed\n");
		return PTR_ERR(syscon->regmap);
	}

	syscon->dev = dev;
	platform_set_drvdata(pdev, syscon);

	dev_info(dev, "syscon regmap start 0x%x end 0x%x registered\n",
		res.start, res.end);

	return 0;
}

static int __devexit syscon_remove(struct platform_device *pdev)
{
	struct syscon *syscon;

	syscon = platform_get_drvdata(pdev);
	iounmap(syscon->base);
	platform_set_drvdata(pdev, NULL);

	return 0;
}

static struct platform_driver syscon_driver = {
	.driver = {
		.name = "syscon",
		.owner = THIS_MODULE,
		.of_match_table = of_syscon_match,
	},
	.probe		= syscon_probe,
	.remove		= __devexit_p(syscon_remove),
};

static int __init syscon_init(void)
{
	return platform_driver_register(&syscon_driver);
}
postcore_initcall(syscon_init);

static void __exit syscon_exit(void)
{
	platform_driver_unregister(&syscon_driver);
}
module_exit(syscon_exit);

MODULE_AUTHOR("Dong Aisheng <dong.aisheng@linaro.org>");
MODULE_DESCRIPTION("System Control driver");
MODULE_LICENSE("GPL v2");
