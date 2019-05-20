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
#include <linux/io.h>

#include <linux/completion.h>
#include <linux/mfd/core.h>

#include <linux/hwmon.h>

struct jz4740_hwmon {
	void __iomem *base;
	int irq;
	const struct mfd_cell *cell;
	struct platform_device *pdev;
	struct completion read_completion;
	struct mutex lock;
};

static irqreturn_t jz4740_hwmon_irq(int irq, void *data)
{
	struct jz4740_hwmon *hwmon = data;

	complete(&hwmon->read_completion);
	return IRQ_HANDLED;
}

static ssize_t in0_input_show(struct device *dev,
			      struct device_attribute *dev_attr, char *buf)
{
	struct jz4740_hwmon *hwmon = dev_get_drvdata(dev);
	struct platform_device *pdev = hwmon->pdev;
	struct completion *completion = &hwmon->read_completion;
	long t;
	unsigned long val;
	int ret;

	mutex_lock(&hwmon->lock);

	reinit_completion(completion);

	enable_irq(hwmon->irq);
	hwmon->cell->enable(pdev);

	t = wait_for_completion_interruptible_timeout(completion, HZ);

	if (t > 0) {
		val = readw(hwmon->base) & 0xfff;
		val = (val * 3300) >> 12;
		ret = sprintf(buf, "%lu\n", val);
	} else {
		ret = t ? t : -ETIMEDOUT;
	}

	hwmon->cell->disable(pdev);
	disable_irq(hwmon->irq);

	mutex_unlock(&hwmon->lock);

	return ret;
}

static DEVICE_ATTR_RO(in0_input);

static struct attribute *jz4740_attrs[] = {
	&dev_attr_in0_input.attr,
	NULL
};

ATTRIBUTE_GROUPS(jz4740);

static int jz4740_hwmon_probe(struct platform_device *pdev)
{
	int ret;
	struct device *dev = &pdev->dev;
	struct jz4740_hwmon *hwmon;
	struct device *hwmon_dev;

	hwmon = devm_kzalloc(dev, sizeof(*hwmon), GFP_KERNEL);
	if (!hwmon)
		return -ENOMEM;

	hwmon->cell = mfd_get_cell(pdev);

	hwmon->irq = platform_get_irq(pdev, 0);
	if (hwmon->irq < 0) {
		dev_err(&pdev->dev, "Failed to get platform irq: %d\n",
			hwmon->irq);
		return hwmon->irq;
	}

	hwmon->base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(hwmon->base))
		return PTR_ERR(hwmon->base);

	hwmon->pdev = pdev;
	init_completion(&hwmon->read_completion);
	mutex_init(&hwmon->lock);

	ret = devm_request_irq(dev, hwmon->irq, jz4740_hwmon_irq, 0,
			       pdev->name, hwmon);
	if (ret) {
		dev_err(&pdev->dev, "Failed to request irq: %d\n", ret);
		return ret;
	}
	disable_irq(hwmon->irq);

	hwmon_dev = devm_hwmon_device_register_with_groups(dev, "jz4740", hwmon,
							   jz4740_groups);
	return PTR_ERR_OR_ZERO(hwmon_dev);
}

static struct platform_driver jz4740_hwmon_driver = {
	.probe	= jz4740_hwmon_probe,
	.driver = {
		.name = "jz4740-hwmon",
	},
};

module_platform_driver(jz4740_hwmon_driver);

MODULE_DESCRIPTION("JZ4740 SoC HWMON driver");
MODULE_AUTHOR("Lars-Peter Clausen <lars@metafoo.de>");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:jz4740-hwmon");
