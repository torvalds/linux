/*
 * Copyright (C) 2009-2010, Lars-Peter Clausen <lars@metafoo.de>
 * JZ4740 SoC HWMON driver
 *
 * This program is free software; you can redistribute it and/or modify it
 * under  the terms of the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the License, or (at your
 * option) any later version.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 */

#include <linux/err.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/platform_device.h>
#include <linux/slab.h>

#include <linux/completion.h>
#include <linux/mfd/core.h>

#include <linux/hwmon.h>

struct jz4740_hwmon {
	struct resource *mem;
	void __iomem *base;

	int irq;

	const struct mfd_cell *cell;
	struct device *hwmon;

	struct completion read_completion;

	struct mutex lock;
};

static ssize_t jz4740_hwmon_show_name(struct device *dev,
	struct device_attribute *dev_attr, char *buf)
{
	return sprintf(buf, "jz4740\n");
}

static irqreturn_t jz4740_hwmon_irq(int irq, void *data)
{
	struct jz4740_hwmon *hwmon = data;

	complete(&hwmon->read_completion);
	return IRQ_HANDLED;
}

static ssize_t jz4740_hwmon_read_adcin(struct device *dev,
	struct device_attribute *dev_attr, char *buf)
{
	struct jz4740_hwmon *hwmon = dev_get_drvdata(dev);
	struct completion *completion = &hwmon->read_completion;
	long t;
	unsigned long val;
	int ret;

	mutex_lock(&hwmon->lock);

	INIT_COMPLETION(*completion);

	enable_irq(hwmon->irq);
	hwmon->cell->enable(to_platform_device(dev));

	t = wait_for_completion_interruptible_timeout(completion, HZ);

	if (t > 0) {
		val = readw(hwmon->base) & 0xfff;
		val = (val * 3300) >> 12;
		ret = sprintf(buf, "%lu\n", val);
	} else {
		ret = t ? t : -ETIMEDOUT;
	}

	hwmon->cell->disable(to_platform_device(dev));
	disable_irq(hwmon->irq);

	mutex_unlock(&hwmon->lock);

	return ret;
}

static DEVICE_ATTR(name, S_IRUGO, jz4740_hwmon_show_name, NULL);
static DEVICE_ATTR(in0_input, S_IRUGO, jz4740_hwmon_read_adcin, NULL);

static struct attribute *jz4740_hwmon_attributes[] = {
	&dev_attr_name.attr,
	&dev_attr_in0_input.attr,
	NULL
};

static const struct attribute_group jz4740_hwmon_attr_group = {
	.attrs = jz4740_hwmon_attributes,
};

static int __devinit jz4740_hwmon_probe(struct platform_device *pdev)
{
	int ret;
	struct jz4740_hwmon *hwmon;

	hwmon = kmalloc(sizeof(*hwmon), GFP_KERNEL);
	if (!hwmon) {
		dev_err(&pdev->dev, "Failed to allocate driver structure\n");
		return -ENOMEM;
	}

	hwmon->cell = mfd_get_cell(pdev);

	hwmon->irq = platform_get_irq(pdev, 0);
	if (hwmon->irq < 0) {
		ret = hwmon->irq;
		dev_err(&pdev->dev, "Failed to get platform irq: %d\n", ret);
		goto err_free;
	}

	hwmon->mem = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!hwmon->mem) {
		ret = -ENOENT;
		dev_err(&pdev->dev, "Failed to get platform mmio resource\n");
		goto err_free;
	}

	hwmon->mem = request_mem_region(hwmon->mem->start,
			resource_size(hwmon->mem), pdev->name);
	if (!hwmon->mem) {
		ret = -EBUSY;
		dev_err(&pdev->dev, "Failed to request mmio memory region\n");
		goto err_free;
	}

	hwmon->base = ioremap_nocache(hwmon->mem->start,
			resource_size(hwmon->mem));
	if (!hwmon->base) {
		ret = -EBUSY;
		dev_err(&pdev->dev, "Failed to ioremap mmio memory\n");
		goto err_release_mem_region;
	}

	init_completion(&hwmon->read_completion);
	mutex_init(&hwmon->lock);

	platform_set_drvdata(pdev, hwmon);

	ret = request_irq(hwmon->irq, jz4740_hwmon_irq, 0, pdev->name, hwmon);
	if (ret) {
		dev_err(&pdev->dev, "Failed to request irq: %d\n", ret);
		goto err_iounmap;
	}
	disable_irq(hwmon->irq);

	ret = sysfs_create_group(&pdev->dev.kobj, &jz4740_hwmon_attr_group);
	if (ret) {
		dev_err(&pdev->dev, "Failed to create sysfs group: %d\n", ret);
		goto err_free_irq;
	}

	hwmon->hwmon = hwmon_device_register(&pdev->dev);
	if (IS_ERR(hwmon->hwmon)) {
		ret = PTR_ERR(hwmon->hwmon);
		goto err_remove_file;
	}

	return 0;

err_remove_file:
	sysfs_remove_group(&pdev->dev.kobj, &jz4740_hwmon_attr_group);
err_free_irq:
	free_irq(hwmon->irq, hwmon);
err_iounmap:
	platform_set_drvdata(pdev, NULL);
	iounmap(hwmon->base);
err_release_mem_region:
	release_mem_region(hwmon->mem->start, resource_size(hwmon->mem));
err_free:
	kfree(hwmon);

	return ret;
}

static int __devexit jz4740_hwmon_remove(struct platform_device *pdev)
{
	struct jz4740_hwmon *hwmon = platform_get_drvdata(pdev);

	hwmon_device_unregister(hwmon->hwmon);
	sysfs_remove_group(&pdev->dev.kobj, &jz4740_hwmon_attr_group);

	free_irq(hwmon->irq, hwmon);

	iounmap(hwmon->base);
	release_mem_region(hwmon->mem->start, resource_size(hwmon->mem));

	platform_set_drvdata(pdev, NULL);
	kfree(hwmon);

	return 0;
}

static struct platform_driver jz4740_hwmon_driver = {
	.probe	= jz4740_hwmon_probe,
	.remove = __devexit_p(jz4740_hwmon_remove),
	.driver = {
		.name = "jz4740-hwmon",
		.owner = THIS_MODULE,
	},
};

module_platform_driver(jz4740_hwmon_driver);

MODULE_DESCRIPTION("JZ4740 SoC HWMON driver");
MODULE_AUTHOR("Lars-Peter Clausen <lars@metafoo.de>");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:jz4740-hwmon");
