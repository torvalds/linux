/*
 * Support for TI bq24022 (bqTINY-II) Dual Input (USB/AC Adpater)
 * 1-Cell Li-Ion Charger connected via GPIOs.
 *
 * Copyright (c) 2008 Philipp Zabel
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/err.h>
#include <linux/gpio.h>
#include <linux/regulator/bq24022.h>
#include <linux/regulator/driver.h>


static int bq24022_set_current_limit(struct regulator_dev *rdev,
					int min_uA, int max_uA)
{
	struct bq24022_mach_info *pdata = rdev_get_drvdata(rdev);

	dev_dbg(rdev_get_dev(rdev), "setting current limit to %s mA\n",
		max_uA >= 500000 ? "500" : "100");

	/* REVISIT: maybe return error if min_uA != 0 ? */
	gpio_set_value(pdata->gpio_iset2, max_uA >= 500000);
	return 0;
}

static int bq24022_get_current_limit(struct regulator_dev *rdev)
{
	struct bq24022_mach_info *pdata = rdev_get_drvdata(rdev);

	return gpio_get_value(pdata->gpio_iset2) ? 500000 : 100000;
}

static int bq24022_enable(struct regulator_dev *rdev)
{
	struct bq24022_mach_info *pdata = rdev_get_drvdata(rdev);

	dev_dbg(rdev_get_dev(rdev), "enabling charger\n");

	gpio_set_value(pdata->gpio_nce, 0);
	return 0;
}

static int bq24022_disable(struct regulator_dev *rdev)
{
	struct bq24022_mach_info *pdata = rdev_get_drvdata(rdev);

	dev_dbg(rdev_get_dev(rdev), "disabling charger\n");

	gpio_set_value(pdata->gpio_nce, 1);
	return 0;
}

static int bq24022_is_enabled(struct regulator_dev *rdev)
{
	struct bq24022_mach_info *pdata = rdev_get_drvdata(rdev);

	return !gpio_get_value(pdata->gpio_nce);
}

static struct regulator_ops bq24022_ops = {
	.set_current_limit = bq24022_set_current_limit,
	.get_current_limit = bq24022_get_current_limit,
	.enable            = bq24022_enable,
	.disable           = bq24022_disable,
	.is_enabled        = bq24022_is_enabled,
};

static struct regulator_desc bq24022_desc = {
	.name  = "bq24022",
	.ops   = &bq24022_ops,
	.type  = REGULATOR_CURRENT,
};

static int __init bq24022_probe(struct platform_device *pdev)
{
	struct bq24022_mach_info *pdata = pdev->dev.platform_data;
	struct regulator_dev *bq24022;
	int ret;

	if (!pdata || !pdata->gpio_nce || !pdata->gpio_iset2)
		return -EINVAL;

	ret = gpio_request(pdata->gpio_nce, "ncharge_en");
	if (ret) {
		dev_dbg(&pdev->dev, "couldn't request nCE GPIO: %d\n",
			pdata->gpio_nce);
		goto err_ce;
	}
	ret = gpio_request(pdata->gpio_iset2, "charge_mode");
	if (ret) {
		dev_dbg(&pdev->dev, "couldn't request ISET2 GPIO: %d\n",
			pdata->gpio_iset2);
		goto err_iset2;
	}
	ret = gpio_direction_output(pdata->gpio_iset2, 0);
	ret = gpio_direction_output(pdata->gpio_nce, 1);

	bq24022 = regulator_register(&bq24022_desc, &pdev->dev,
				     pdata->init_data, pdata);
	if (IS_ERR(bq24022)) {
		dev_dbg(&pdev->dev, "couldn't register regulator\n");
		ret = PTR_ERR(bq24022);
		goto err_reg;
	}
	platform_set_drvdata(pdev, bq24022);
	dev_dbg(&pdev->dev, "registered regulator\n");

	return 0;
err_reg:
	gpio_free(pdata->gpio_iset2);
err_iset2:
	gpio_free(pdata->gpio_nce);
err_ce:
	return ret;
}

static int __devexit bq24022_remove(struct platform_device *pdev)
{
	struct bq24022_mach_info *pdata = pdev->dev.platform_data;
	struct regulator_dev *bq24022 = platform_get_drvdata(pdev);

	regulator_unregister(bq24022);
	gpio_free(pdata->gpio_iset2);
	gpio_free(pdata->gpio_nce);

	return 0;
}

static struct platform_driver bq24022_driver = {
	.driver = {
		.name = "bq24022",
	},
	.remove = __devexit_p(bq24022_remove),
};

static int __init bq24022_init(void)
{
	return platform_driver_probe(&bq24022_driver, bq24022_probe);
}

static void __exit bq24022_exit(void)
{
	platform_driver_unregister(&bq24022_driver);
}

module_init(bq24022_init);
module_exit(bq24022_exit);

MODULE_AUTHOR("Philipp Zabel");
MODULE_DESCRIPTION("TI bq24022 Li-Ion Charger driver");
MODULE_LICENSE("GPL");
