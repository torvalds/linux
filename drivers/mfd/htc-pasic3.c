/*
 * Core driver for HTC PASIC3 LED/DS1WM chip.
 *
 * Copyright (C) 2006 Philipp Zabel <philipp.zabel@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/platform_device.h>

#include <linux/ds1wm.h>
#include <linux/gpio.h>
#include <linux/io.h>
#include <linux/irq.h>
#include <linux/interrupt.h>
#include <linux/mfd/htc-pasic3.h>

struct pasic3_data {
	void __iomem *mapping;
	unsigned int bus_shift;
	struct platform_device *ds1wm_pdev;
	struct platform_device *led_pdev;
};

#define REG_ADDR  5
#define REG_DATA  6

#define READ_MODE 0x80

/*
 * write to a secondary register on the PASIC3
 */
void pasic3_write_register(struct device *dev, u32 reg, u8 val)
{
	struct pasic3_data *asic = dev->driver_data;
	int bus_shift = asic->bus_shift;
	void __iomem *addr = asic->mapping + (REG_ADDR << bus_shift);
	void __iomem *data = asic->mapping + (REG_DATA << bus_shift);

	__raw_writeb(~READ_MODE & reg, addr);
	__raw_writeb(val, data);
}
EXPORT_SYMBOL(pasic3_write_register); /* for leds-pasic3 */

/*
 * read from a secondary register on the PASIC3
 */
u8 pasic3_read_register(struct device *dev, u32 reg)
{
	struct pasic3_data *asic = dev->driver_data;
	int bus_shift = asic->bus_shift;
	void __iomem *addr = asic->mapping + (REG_ADDR << bus_shift);
	void __iomem *data = asic->mapping + (REG_DATA << bus_shift);

	__raw_writeb(READ_MODE | reg, addr);
	return __raw_readb(data);
}
EXPORT_SYMBOL(pasic3_read_register); /* for leds-pasic3 */

/*
 * LEDs
 */

static int led_device_add(struct device *pasic3_dev,
				const struct pasic3_leds_machinfo *pdata)
{
	struct pasic3_data *asic = pasic3_dev->driver_data;
	struct platform_device *pdev;
	int ret;

	pdev = platform_device_alloc("pasic3-led", -1);
	if (!pdev) {
		dev_dbg(pasic3_dev, "failed to allocate LED platform device\n");
		return -ENOMEM;
	}

	ret = platform_device_add_data(pdev, pdata,
					sizeof(struct pasic3_leds_machinfo));
	if (ret < 0) {
		dev_dbg(pasic3_dev, "failed to add LED platform data\n");
		goto exit_pdev_put;
	}

	pdev->dev.parent = pasic3_dev;
	ret = platform_device_add(pdev);
	if (ret < 0) {
		dev_dbg(pasic3_dev, "failed to add LED platform device\n");
		goto exit_pdev_put;
	}

	asic->led_pdev = pdev;
	return 0;

exit_pdev_put:
	platform_device_put(pdev);
	return ret;
}

/*
 * DS1WM
 */

static void ds1wm_enable(struct platform_device *pdev)
{
	struct device *dev = pdev->dev.parent;
	int c;

	c = pasic3_read_register(dev, 0x28);
	pasic3_write_register(dev, 0x28, c & 0x7f);

	dev_dbg(dev, "DS1WM OWM_EN low (active) %02x\n", c & 0x7f);
}

static void ds1wm_disable(struct platform_device *pdev)
{
	struct device *dev = pdev->dev.parent;
	int c;

	c = pasic3_read_register(dev, 0x28);
	pasic3_write_register(dev, 0x28, c | 0x80);

	dev_dbg(dev, "DS1WM OWM_EN high (inactive) %02x\n", c | 0x80);
}

static struct ds1wm_platform_data ds1wm_pdata = {
	.bus_shift = 2,
	.enable    = ds1wm_enable,
	.disable   = ds1wm_disable,
};

static int ds1wm_device_add(struct platform_device *pasic3_pdev, int bus_shift)
{
	struct device *pasic3_dev = &pasic3_pdev->dev;
	struct pasic3_data *asic = pasic3_dev->driver_data;
	struct platform_device *pdev;
	int ret;

	pdev = platform_device_alloc("ds1wm", -1);
	if (!pdev) {
		dev_dbg(pasic3_dev, "failed to allocate DS1WM platform device\n");
		return -ENOMEM;
	}

	ret = platform_device_add_resources(pdev, pasic3_pdev->resource,
						pasic3_pdev->num_resources);
	if (ret < 0) {
		dev_dbg(pasic3_dev, "failed to add DS1WM resources\n");
		goto exit_pdev_put;
	}

	ds1wm_pdata.bus_shift = asic->bus_shift;
	ret = platform_device_add_data(pdev, &ds1wm_pdata,
					sizeof(struct ds1wm_platform_data));
	if (ret < 0) {
		dev_dbg(pasic3_dev, "failed to add DS1WM platform data\n");
		goto exit_pdev_put;
	}

	pdev->dev.parent = pasic3_dev;
	ret = platform_device_add(pdev);
	if (ret < 0) {
		dev_dbg(pasic3_dev, "failed to add DS1WM platform device\n");
		goto exit_pdev_put;
	}

	asic->ds1wm_pdev = pdev;
	return 0;

exit_pdev_put:
	platform_device_put(pdev);
	return ret;
}

static int __init pasic3_probe(struct platform_device *pdev)
{
	struct pasic3_platform_data *pdata = pdev->dev.platform_data;
	struct device *dev = &pdev->dev;
	struct pasic3_data *asic;
	struct resource *r;
	int ret;

	r = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!r)
		return -ENXIO;

	if (!request_mem_region(r->start, r->end - r->start + 1, "pasic3"))
		return -EBUSY;

	asic = kzalloc(sizeof(struct pasic3_data), GFP_KERNEL);
	if (!asic)
		return -ENOMEM;

	platform_set_drvdata(pdev, asic);

	if (pdata && pdata->bus_shift)
		asic->bus_shift = pdata->bus_shift;
	else
		asic->bus_shift = 2;

	asic->mapping = ioremap(r->start, r->end - r->start + 1);
	if (!asic->mapping) {
		dev_err(dev, "couldn't ioremap PASIC3\n");
		kfree(asic);
		return -ENOMEM;
	}

	ret = ds1wm_device_add(pdev, asic->bus_shift);
	if (ret < 0)
		dev_warn(dev, "failed to register DS1WM\n");

	if (pdata->led_pdata) {
		ret = led_device_add(dev, pdata->led_pdata);
		if (ret < 0)
			dev_warn(dev, "failed to register LED device\n");
	}

	return 0;
}

static int pasic3_remove(struct platform_device *pdev)
{
	struct pasic3_data *asic = platform_get_drvdata(pdev);
	struct resource *r;

	if (asic->led_pdev)
		platform_device_unregister(asic->led_pdev);
	if (asic->ds1wm_pdev)
		platform_device_unregister(asic->ds1wm_pdev);

	iounmap(asic->mapping);
	r = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	release_mem_region(r->start, r->end - r->start + 1);
	kfree(asic);
	return 0;
}

static struct platform_driver pasic3_driver = {
	.driver		= {
		.name	= "pasic3",
	},
	.remove		= pasic3_remove,
};

static int __init pasic3_base_init(void)
{
	return platform_driver_probe(&pasic3_driver, pasic3_probe);
}

static void __exit pasic3_base_exit(void)
{
	platform_driver_unregister(&pasic3_driver);
}

module_init(pasic3_base_init);
module_exit(pasic3_base_exit);

MODULE_AUTHOR("Philipp Zabel <philipp.zabel@gmail.com>");
MODULE_DESCRIPTION("Core driver for HTC PASIC3");
MODULE_LICENSE("GPL");
