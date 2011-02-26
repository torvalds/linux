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
#include <linux/spi/spi.h>

#include <linux/mfd/wm831x/core.h>

static int wm831x_spi_read_device(struct wm831x *wm831x, unsigned short reg,
				  int bytes, void *dest)
{
	u16 tx_val;
	u16 *d = dest;
	int r, ret;

	/* Go register at a time */
	for (r = reg; r < reg + (bytes / 2); r++) {
		tx_val = r | 0x8000;

		ret = spi_write_then_read(wm831x->control_data,
					  (u8 *)&tx_val, 2, (u8 *)d, 2);
		if (ret != 0)
			return ret;

		*d = be16_to_cpu(*d);

		d++;
	}

	return 0;
}

static int wm831x_spi_write_device(struct wm831x *wm831x, unsigned short reg,
				   int bytes, void *src)
{
	struct spi_device *spi = wm831x->control_data;
	u16 *s = src;
	u16 data[2];
	int ret, r;

	/* Go register at a time */
	for (r = reg; r < reg + (bytes / 2); r++) {
		data[0] = r;
		data[1] = *s++;

		ret = spi_write(spi, (char *)&data, sizeof(data));
		if (ret != 0)
			return ret;
	}

	return 0;
}

static int __devinit wm831x_spi_probe(struct spi_device *spi)
{
	struct wm831x *wm831x;
	enum wm831x_parent type;

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
	wm831x->control_data = spi;
	wm831x->read_dev = wm831x_spi_read_device;
	wm831x->write_dev = wm831x_spi_write_device;

	return wm831x_device_init(wm831x, type, spi->irq);
}

static int __devexit wm831x_spi_remove(struct spi_device *spi)
{
	struct wm831x *wm831x = dev_get_drvdata(&spi->dev);

	wm831x_device_exit(wm831x);

	return 0;
}

static int wm831x_spi_suspend(struct spi_device *spi, pm_message_t m)
{
	struct wm831x *wm831x = dev_get_drvdata(&spi->dev);

	return wm831x_device_suspend(wm831x);
}

static struct spi_driver wm8310_spi_driver = {
	.driver = {
		.name	= "wm8310",
		.bus	= &spi_bus_type,
		.owner	= THIS_MODULE,
	},
	.probe		= wm831x_spi_probe,
	.remove		= __devexit_p(wm831x_spi_remove),
	.suspend	= wm831x_spi_suspend,
};

static struct spi_driver wm8311_spi_driver = {
	.driver = {
		.name	= "wm8311",
		.bus	= &spi_bus_type,
		.owner	= THIS_MODULE,
	},
	.probe		= wm831x_spi_probe,
	.remove		= __devexit_p(wm831x_spi_remove),
	.suspend	= wm831x_spi_suspend,
};

static struct spi_driver wm8312_spi_driver = {
	.driver = {
		.name	= "wm8312",
		.bus	= &spi_bus_type,
		.owner	= THIS_MODULE,
	},
	.probe		= wm831x_spi_probe,
	.remove		= __devexit_p(wm831x_spi_remove),
	.suspend	= wm831x_spi_suspend,
};

static struct spi_driver wm8320_spi_driver = {
	.driver = {
		.name	= "wm8320",
		.bus	= &spi_bus_type,
		.owner	= THIS_MODULE,
	},
	.probe		= wm831x_spi_probe,
	.remove		= __devexit_p(wm831x_spi_remove),
	.suspend	= wm831x_spi_suspend,
};

static struct spi_driver wm8321_spi_driver = {
	.driver = {
		.name	= "wm8321",
		.bus	= &spi_bus_type,
		.owner	= THIS_MODULE,
	},
	.probe		= wm831x_spi_probe,
	.remove		= __devexit_p(wm831x_spi_remove),
	.suspend	= wm831x_spi_suspend,
};

static struct spi_driver wm8325_spi_driver = {
	.driver = {
		.name	= "wm8325",
		.bus	= &spi_bus_type,
		.owner	= THIS_MODULE,
	},
	.probe		= wm831x_spi_probe,
	.remove		= __devexit_p(wm831x_spi_remove),
	.suspend	= wm831x_spi_suspend,
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

	return 0;
}
subsys_initcall(wm831x_spi_init);

static void __exit wm831x_spi_exit(void)
{
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
