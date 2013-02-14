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

static int wm831x_spi_probe(struct spi_device *spi)
{
	const struct spi_device_id *id = spi_get_device_id(spi);
	struct wm831x *wm831x;
	enum wm831x_parent type;
	int ret;

	type = (enum wm831x_parent)id->driver_data;

	wm831x = devm_kzalloc(&spi->dev, sizeof(struct wm831x), GFP_KERNEL);
	if (wm831x == NULL)
		return -ENOMEM;

	spi->bits_per_word = 16;
	spi->mode = SPI_MODE_0;

	dev_set_drvdata(&spi->dev, wm831x);
	wm831x->dev = &spi->dev;

	wm831x->regmap = devm_regmap_init_spi(spi, &wm831x_regmap_config);
	if (IS_ERR(wm831x->regmap)) {
		ret = PTR_ERR(wm831x->regmap);
		dev_err(wm831x->dev, "Failed to allocate register map: %d\n",
			ret);
		return ret;
	}

	return wm831x_device_init(wm831x, type, spi->irq);
}

static int wm831x_spi_remove(struct spi_device *spi)
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

static void wm831x_spi_shutdown(struct spi_device *spi)
{
	struct wm831x *wm831x = dev_get_drvdata(&spi->dev);

	wm831x_device_shutdown(wm831x);
}

static const struct dev_pm_ops wm831x_spi_pm = {
	.freeze = wm831x_spi_suspend,
	.suspend = wm831x_spi_suspend,
};

static const struct spi_device_id wm831x_spi_ids[] = {
	{ "wm8310", WM8310 },
	{ "wm8311", WM8311 },
	{ "wm8312", WM8312 },
	{ "wm8320", WM8320 },
	{ "wm8321", WM8321 },
	{ "wm8325", WM8325 },
	{ "wm8326", WM8326 },
	{ },
};
MODULE_DEVICE_TABLE(spi, wm831x_spi_ids);

static struct spi_driver wm831x_spi_driver = {
	.driver = {
		.name	= "wm831x",
		.owner	= THIS_MODULE,
		.pm	= &wm831x_spi_pm,
	},
	.id_table	= wm831x_spi_ids,
	.probe		= wm831x_spi_probe,
	.remove		= wm831x_spi_remove,
	.shutdown	= wm831x_spi_shutdown,
};

static int __init wm831x_spi_init(void)
{
	int ret;

	ret = spi_register_driver(&wm831x_spi_driver);
	if (ret != 0)
		pr_err("Failed to register WM831x SPI driver: %d\n", ret);

	return 0;
}
subsys_initcall(wm831x_spi_init);

static void __exit wm831x_spi_exit(void)
{
	spi_unregister_driver(&wm831x_spi_driver);
}
module_exit(wm831x_spi_exit);

MODULE_DESCRIPTION("SPI support for WM831x/2x AudioPlus PMICs");
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Mark Brown");
