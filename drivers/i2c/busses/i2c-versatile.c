/*
 *  i2c-versatile.c
 *
 *  Copyright (C) 2006 ARM Ltd.
 *  written by Russell King, Deep Blue Solutions Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/i2c.h>
#include <linux/i2c-algo-bit.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/io.h>

#define I2C_CONTROL	0x00
#define I2C_CONTROLS	0x00
#define I2C_CONTROLC	0x04
#define SCL		(1 << 0)
#define SDA		(1 << 1)

struct i2c_versatile {
	struct i2c_adapter	 adap;
	struct i2c_algo_bit_data algo;
	void __iomem		 *base;
};

static void i2c_versatile_setsda(void *data, int state)
{
	struct i2c_versatile *i2c = data;

	writel(SDA, i2c->base + (state ? I2C_CONTROLS : I2C_CONTROLC));
}

static void i2c_versatile_setscl(void *data, int state)
{
	struct i2c_versatile *i2c = data;

	writel(SCL, i2c->base + (state ? I2C_CONTROLS : I2C_CONTROLC));
}

static int i2c_versatile_getsda(void *data)
{
	struct i2c_versatile *i2c = data;
	return !!(readl(i2c->base + I2C_CONTROL) & SDA);
}

static int i2c_versatile_getscl(void *data)
{
	struct i2c_versatile *i2c = data;
	return !!(readl(i2c->base + I2C_CONTROL) & SCL);
}

static const struct i2c_algo_bit_data i2c_versatile_algo = {
	.setsda	= i2c_versatile_setsda,
	.setscl = i2c_versatile_setscl,
	.getsda	= i2c_versatile_getsda,
	.getscl = i2c_versatile_getscl,
	.udelay	= 30,
	.timeout = HZ,
};

static int i2c_versatile_probe(struct platform_device *dev)
{
	struct i2c_versatile *i2c;
	struct resource *r;
	int ret;

	i2c = devm_kzalloc(&dev->dev, sizeof(struct i2c_versatile), GFP_KERNEL);
	if (!i2c)
		return -ENOMEM;

	r = platform_get_resource(dev, IORESOURCE_MEM, 0);
	i2c->base = devm_ioremap_resource(&dev->dev, r);
	if (IS_ERR(i2c->base))
		return PTR_ERR(i2c->base);

	writel(SCL | SDA, i2c->base + I2C_CONTROLS);

	i2c->adap.owner = THIS_MODULE;
	strlcpy(i2c->adap.name, "Versatile I2C adapter", sizeof(i2c->adap.name));
	i2c->adap.algo_data = &i2c->algo;
	i2c->adap.dev.parent = &dev->dev;
	i2c->adap.dev.of_node = dev->dev.of_node;
	i2c->algo = i2c_versatile_algo;
	i2c->algo.data = i2c;

	i2c->adap.nr = dev->id;
	ret = i2c_bit_add_numbered_bus(&i2c->adap);
	if (ret < 0)
		return ret;

	platform_set_drvdata(dev, i2c);

	return 0;
}

static int i2c_versatile_remove(struct platform_device *dev)
{
	struct i2c_versatile *i2c = platform_get_drvdata(dev);

	i2c_del_adapter(&i2c->adap);
	return 0;
}

static const struct of_device_id i2c_versatile_match[] = {
	{ .compatible = "arm,versatile-i2c", },
	{},
};
MODULE_DEVICE_TABLE(of, i2c_versatile_match);

static struct platform_driver i2c_versatile_driver = {
	.probe		= i2c_versatile_probe,
	.remove		= i2c_versatile_remove,
	.driver		= {
		.name	= "versatile-i2c",
		.of_match_table = i2c_versatile_match,
	},
};

static int __init i2c_versatile_init(void)
{
	return platform_driver_register(&i2c_versatile_driver);
}

static void __exit i2c_versatile_exit(void)
{
	platform_driver_unregister(&i2c_versatile_driver);
}

subsys_initcall(i2c_versatile_init);
module_exit(i2c_versatile_exit);

MODULE_DESCRIPTION("ARM Versatile I2C bus driver");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:versatile-i2c");
