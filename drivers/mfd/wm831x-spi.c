/*
 * wm831x-spi.c  --  SPI access for Wolfson WM831x PMICs
 *
 * Copyright 2009,2010 Wolfson Microelectronics PLC.
 *
 * Author: Mark Brown <broonie@opensource.wolfsonmicro.com>
 *
 *  This program is free software; you can redistribute  it and/or modify it
 *  under  the terms of  the GNU General  Public License as published by the
 *  Free Software Foundation;  either version 2 of the  License, or (at your
 *  option) any later version.
 *
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/pm.h>
#include <linux/spi/spi.h>
#include <linux/regmap.h>
#include <linux/err.h>

#include <linux/mfd/wm831x/core.h>

static int __devinit wm831x_spi_probe(struct spi_device *spi)
{
	struct wm831x *wm831x;
	enum wm831x_parent type;
	int ret;

	/* Currently SPI support for ID tables is unmerged, we're faking it */
	if (strcmp(spi->modalias, "wm8310") == 0)
		type = WM8310;
	else if (strcmp(spi->modalias, "wm8311") == 0)
		type = WM8311;
	else if (strcmp(spi->modalias, "wm8312") == 0)
		type = WM8312;
	else if (strcmp(spi->modalias, "wm8320") == 0)
		type = WM8320;
	else if (strcmp(spi->modalias, "wm8321") == 0)
		type = WM8321;
	else if (strcmp(spi->modalias, "wm8325") == 0)
		type = WM8325;
	else if (strcmp(spi->modalias, "wm8326") == 0)
		type = WM8326;
	else {
		dev_err(&spi->dev, "Unknown device type\n");
		return -EINVAL;
	}

	wm831x = kzalloc(sizeof(struct wm831x), GFP_KERNEL);
	if (wm831x == NULL)
		return -ENOMEM;

	spi->bits_per_word = 16;
	spi->mode = SPI_MODE_0;

	dev_set_drvdata(&spi->dev, wm831x);
	wm831x->dev = &spi->dev;

	wm831x->regmap = regmap_init_spi(wm831x->dev, &wm831x_regmap_config);
	if (IS_ERR(wm831x->regmap)) {
		ret = PTR_ERR(wm831x->regmap);
		dev_err(wm831x->dev, "Failed to allocate register map: %d\n",
			ret);
		kfree(wm831x);
		return ret;
	}

	return wm831x_device_init(wm831x, type, spi->irq);
}

static int __devexit wm831x_spi_remove(struct spi_device *spi)
{
	struct wm831x *wm831x = dev_get_drvdata(&spi->dev);

	wm831x_device_exit(wm831x);

	return 0;
}

static int wm831x_spi_suspend(struct device *dev)
{
	struct wm831x *wm831x = dev_get_drvdata(dev);

	return wm831x_device_suspend(wm831x);
}

static const struct dev_pm_ops wm831x_spi_pm = {
	.freeze = wm831x_spi_suspend,
	.suspend = wm831x_spi_suspend,
};

static struct spi_driver wm8310_spi_driver = {
	.driver = {
		.name	= "wm8310",
		.bus	= &spi_bus_type,
		.owner	= THIS_MODULE,
		.pm	= &wm831x_spi_pm,
	},
	.probe		= wm831x_spi_probe,
	.remove		= __devexit_p(wm831x_spi_remove),
};

static struct spi_driver wm8311_spi_driver = {
	.driver = {
		.name	= "wm8311",
		.bus	= &spi_bus_type,
		.owner	= THIS_MODULE,
		.pm	= &wm831x_spi_pm,
	},
	.probe		= wm831x_spi_probe,
	.remove		= __devexit_p(wm831x_spi_remove),
};

static struct spi_driver wm8312_spi_driver = {
	.driver = {
		.name	= "wm8312",
		.bus	= &spi_bus_type,
		.owner	= THIS_MODULE,
		.pm	= &wm831x_spi_pm,
	},
	.probe		= wm831x_spi_probe,
	.remove		= __devexit_p(wm831x_spi_remove),
};

static struct spi_driver wm8320_spi_driver = {
	.driver = {
		.name	= "wm8320",
		.bus	= &spi_bus_type,
		.owner	= THIS_MODULE,
		.pm	= &wm831x_spi_pm,
	},
	.probe		= wm831x_spi_probe,
	.remove		= __devexit_p(wm831x_spi_remove),
};

static struct spi_driver wm8321_spi_driver = {
	.driver = {
		.name	= "wm8321",
		.bus	= &spi_bus_type,
		.owner	= THIS_MODULE,
		.pm	= &wm831x_spi_pm,
	},
	.probe		= wm831x_spi_probe,
	.remove		= __devexit_p(wm831x_spi_remove),
};

static struct spi_driver wm8325_spi_driver = {
	.driver = {
		.name	= "wm8325",
		.bus	= &spi_bus_type,
		.owner	= THIS_MODULE,
		.pm	= &wm831x_spi_pm,
	},
	.probe		= wm831x_spi_probe,
	.remove		= __devexit_p(wm831x_spi_remove),
};

static struct spi_driver wm8326_spi_driver = {
	.driver = {
		.name	= "wm8326",
		.bus	= &spi_bus_type,
		.owner	= THIS_MODULE,
		.pm	= &wm831x_spi_pm,
	},
	.probe		= wm831x_spi_probe,
	.remove		= __devexit_p(wm831x_spi_remove),
};

static int __init wm831x_spi_init(void)
{
	int ret;

	ret = spi_register_driver(&wm8310_spi_driver);
	if (ret != 0)
		pr_err("Failed to register WM8310 SPI driver: %d\n", ret);

	ret = spi_register_driver(&wm8311_spi_driver);
	if (ret != 0)
		pr_err("Failed to register WM8311 SPI driver: %d\n", ret);

	ret = spi_register_driver(&wm8312_spi_driver);
	if (ret != 0)
		pr_err("Failed to register WM8312 SPI driver: %d\n", ret);

	ret = spi_register_driver(&wm8320_spi_driver);
	if (ret != 0)
		pr_err("Failed to register WM8320 SPI driver: %d\n", ret);

	ret = spi_register_driver(&wm8321_spi_driver);
	if (ret != 0)
		pr_err("Failed to register WM8321 SPI driver: %d\n", ret);

	ret = spi_register_driver(&wm8325_spi_driver);
	if (ret != 0)
		pr_err("Failed to register WM8325 SPI driver: %d\n", ret);

	ret = spi_register_driver(&wm8326_spi_driver);
	if (ret != 0)
		pr_err("Failed to register WM8326 SPI driver: %d\n", ret);

	return 0;
}
subsys_initcall(wm831x_spi_init);

static void __exit wm831x_spi_exit(void)
{
	spi_unregister_driver(&wm8326_spi_driver);
	spi_unregister_driver(&wm8325_spi_driver);
	spi_unregister_driver(&wm8321_spi_driver);
	spi_unregister_driver(&wm8320_spi_driver);
	spi_unregister_driver(&wm8312_spi_driver);
	spi_unregister_driver(&wm8311_spi_driver);
	spi_unregister_driver(&wm8310_spi_driver);
}
module_exit(wm831x_spi_exit);

MODULE_DESCRIPTION("SPI support for WM831x/2x AudioPlus PMICs");
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Mark Brown");
