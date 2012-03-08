/*
 * Copyright (C) ST-Ericsson SA 2010
 * Author: Mattias Wallin <mattias.wallin@stericsson.com> for ST-Ericsson.
 * License Terms: GNU General Public License v2
 * This file was based on drivers/mfd/ab8500-spi.c
 */

#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/mfd/abx500/ab8500.h>
#include <linux/mfd/dbx500-prcmu.h>

static int ab8500_i2c_write(struct ab8500 *ab8500, u16 addr, u8 data)
{
	int ret;

	ret = prcmu_abb_write((u8)(addr >> 8), (u8)(addr & 0xFF), &data, 1);
	if (ret < 0)
		dev_err(ab8500->dev, "prcmu i2c error %d\n", ret);
	return ret;
}

static int ab8500_i2c_write_masked(struct ab8500 *ab8500, u16 addr, u8 mask,
	u8 data)
{
	int ret;

	ret = prcmu_abb_write_masked((u8)(addr >> 8), (u8)(addr & 0xFF), &data,
		&mask, 1);
	if (ret < 0)
		dev_err(ab8500->dev, "prcmu i2c error %d\n", ret);
	return ret;
}

static int ab8500_i2c_read(struct ab8500 *ab8500, u16 addr)
{
	int ret;
	u8 data;

	ret = prcmu_abb_read((u8)(addr >> 8), (u8)(addr & 0xFF), &data, 1);
	if (ret < 0) {
		dev_err(ab8500->dev, "prcmu i2c error %d\n", ret);
		return ret;
	}
	return (int)data;
}

static int __devinit ab8500_i2c_probe(struct platform_device *plf)
{
	const struct platform_device_id *platid = platform_get_device_id(plf);
	struct ab8500 *ab8500;
	struct resource *resource;
	int ret;

	ab8500 = kzalloc(sizeof *ab8500, GFP_KERNEL);
	if (!ab8500)
		return -ENOMEM;

	ab8500->dev = &plf->dev;

	resource = platform_get_resource(plf, IORESOURCE_IRQ, 0);
	if (!resource) {
		kfree(ab8500);
		return -ENODEV;
	}

	ab8500->irq = resource->start;

	ab8500->read = ab8500_i2c_read;
	ab8500->write = ab8500_i2c_write;
	ab8500->write_masked = ab8500_i2c_write_masked;

	platform_set_drvdata(plf, ab8500);

	ret = ab8500_init(ab8500, platid->driver_data);
	if (ret)
		kfree(ab8500);


	return ret;
}

static int __devexit ab8500_i2c_remove(struct platform_device *plf)
{
	struct ab8500 *ab8500 = platform_get_drvdata(plf);

	ab8500_exit(ab8500);
	kfree(ab8500);

	return 0;
}

static const struct platform_device_id ab8500_id[] = {
	{ "ab8500-i2c", AB8500_VERSION_AB8500 },
	{ "ab8505-i2c", AB8500_VERSION_AB8505 },
	{ "ab9540-i2c", AB8500_VERSION_AB9540 },
	{ "ab8540-i2c", AB8500_VERSION_AB8540 },
	{ }
};

static struct platform_driver ab8500_i2c_driver = {
	.driver = {
		.name = "ab8500-i2c",
		.owner = THIS_MODULE,
	},
	.probe	= ab8500_i2c_probe,
	.remove	= __devexit_p(ab8500_i2c_remove),
	.id_table = ab8500_id,
};

static int __init ab8500_i2c_init(void)
{
	return platform_driver_register(&ab8500_i2c_driver);
}

static void __exit ab8500_i2c_exit(void)
{
	platform_driver_unregister(&ab8500_i2c_driver);
}
arch_initcall(ab8500_i2c_init);
module_exit(ab8500_i2c_exit);

MODULE_AUTHOR("Mattias WALLIN <mattias.wallin@stericsson.com");
MODULE_DESCRIPTION("AB8500 Core access via PRCMU I2C");
MODULE_LICENSE("GPL v2");
