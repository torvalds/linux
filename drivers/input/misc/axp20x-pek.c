/*
 * axp20x power button driver.
 *
 * Copyright (C) 2013 Carlo Caione <carlo@caione.org>
 *
 * This file is subject to the terms and conditions of the GNU General
 * Public License. See the file "COPYING" in the main directory of this
 * archive for more details.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 */

#include <linux/errno.h>
#include <linux/irq.h>
#include <linux/init.h>
#include <linux/input.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/mfd/axp20x.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/slab.h>

#define AXP20X_PEK_STARTUP_MASK		(0xc0)
#define AXP20X_PEK_SHUTDOWN_MASK	(0x03)

struct axp20x_pek {
	struct axp20x_dev *axp20x;
	struct input_dev *input;
	int irq_dbr;
	int irq_dbf;
};

struct axp20x_time {
	unsigned int time;
	unsigned int idx;
};

static const struct axp20x_time startup_time[] = {
	{ .time = 128,  .idx = 0 },
	{ .time = 1000, .idx = 2 },
	{ .time = 3000, .idx = 1 },
	{ .time = 2000, .idx = 3 },
};

static const struct axp20x_time shutdown_time[] = {
	{ .time = 4000,  .idx = 0 },
	{ .time = 6000,  .idx = 1 },
	{ .time = 8000,  .idx = 2 },
	{ .time = 10000, .idx = 3 },
};

struct axp20x_pek_ext_attr {
	const struct axp20x_time *p_time;
	unsigned int mask;
};

static struct axp20x_pek_ext_attr axp20x_pek_startup_ext_attr = {
	.p_time	= startup_time,
	.mask	= AXP20X_PEK_STARTUP_MASK,
};

static struct axp20x_pek_ext_attr axp20x_pek_shutdown_ext_attr = {
	.p_time	= shutdown_time,
	.mask	= AXP20X_PEK_SHUTDOWN_MASK,
};

static struct axp20x_pek_ext_attr *get_axp_ext_attr(struct device_attribute *attr)
{
	return container_of(attr, struct dev_ext_attribute, attr)->var;
}

static ssize_t axp20x_show_ext_attr(struct device *dev,
				    struct device_attribute *attr, char *buf)
{
	struct axp20x_pek *axp20x_pek = dev_get_drvdata(dev);
	struct axp20x_pek_ext_attr *axp20x_ea = get_axp_ext_attr(attr);
	unsigned int val;
	int ret, i;

	ret = regmap_read(axp20x_pek->axp20x->regmap, AXP20X_PEK_KEY, &val);
	if (ret != 0)
		return ret;

	val &= axp20x_ea->mask;
	val >>= ffs(axp20x_ea->mask) - 1;

	for (i = 0; i < 4; i++)
		if (val == axp20x_ea->p_time[i].idx)
			val = axp20x_ea->p_time[i].time;

	return sprintf(buf, "%u\n", val);
}

static ssize_t axp20x_store_ext_attr(struct device *dev,
				     struct device_attribute *attr,
				     const char *buf, size_t count)
{
	struct axp20x_pek *axp20x_pek = dev_get_drvdata(dev);
	struct axp20x_pek_ext_attr *axp20x_ea = get_axp_ext_attr(attr);
	char val_str[20];
	size_t len;
	int ret, i;
	unsigned int val, idx = 0;
	unsigned int best_err = UINT_MAX;

	val_str[sizeof(val_str) - 1] = '\0';
	strncpy(val_str, buf, sizeof(val_str) - 1);
	len = strlen(val_str);

	if (len && val_str[len - 1] == '\n')
		val_str[len - 1] = '\0';

	ret = kstrtouint(val_str, 10, &val);
	if (ret)
		return ret;

	for (i = 3; i >= 0; i--) {
		unsigned int err;

		err = abs(axp20x_ea->p_time[i].time - val);
		if (err < best_err) {
			best_err = err;
			idx = axp20x_ea->p_time[i].idx;
		}

		if (!err)
			break;
	}

	idx <<= ffs(axp20x_ea->mask) - 1;
	ret = regmap_update_bits(axp20x_pek->axp20x->regmap,
				 AXP20X_PEK_KEY,
				 axp20x_ea->mask, idx);
	if (ret != 0)
		return -EINVAL;
	return count;
}

static struct dev_ext_attribute axp20x_dev_attr_startup = {
	.attr	= __ATTR(startup, 0644, axp20x_show_ext_attr, axp20x_store_ext_attr),
	.var	= &axp20x_pek_startup_ext_attr
};

static struct dev_ext_attribute axp20x_dev_attr_shutdown = {
	.attr	= __ATTR(shutdown, 0644, axp20x_show_ext_attr, axp20x_store_ext_attr),
	.var	= &axp20x_pek_shutdown_ext_attr
};

static irqreturn_t axp20x_pek_irq(int irq, void *pwr)
{
	struct input_dev *idev = pwr;
	struct axp20x_pek *axp20x_pek = input_get_drvdata(idev);

	if (irq == axp20x_pek->irq_dbr)
		input_report_key(idev, KEY_POWER, true);
	else if (irq == axp20x_pek->irq_dbf)
		input_report_key(idev, KEY_POWER, false);

	input_sync(idev);

	return IRQ_HANDLED;
}

static int axp20x_pek_probe(struct platform_device *pdev)
{
	struct axp20x_pek *axp20x_pek;
	struct axp20x_dev *axp20x;
	struct input_dev *idev;
	int error;

	axp20x_pek = devm_kzalloc(&pdev->dev, sizeof(struct axp20x_pek),
				  GFP_KERNEL);
	if (!axp20x_pek)
		return -ENOMEM;

	axp20x_pek->axp20x = dev_get_drvdata(pdev->dev.parent);
	axp20x = axp20x_pek->axp20x;

	axp20x_pek->irq_dbr = platform_get_irq_byname(pdev, "PEK_DBR");
	if (axp20x_pek->irq_dbr < 0) {
		dev_err(&pdev->dev, "No IRQ for PEK_DBR, error=%d\n",
				axp20x_pek->irq_dbr);
		return axp20x_pek->irq_dbr;
	}
	axp20x_pek->irq_dbr = regmap_irq_get_virq(axp20x->regmap_irqc,
						  axp20x_pek->irq_dbr);

	axp20x_pek->irq_dbf = platform_get_irq_byname(pdev, "PEK_DBF");
	if (axp20x_pek->irq_dbf < 0) {
		dev_err(&pdev->dev, "No IRQ for PEK_DBF, error=%d\n",
				axp20x_pek->irq_dbf);
		return axp20x_pek->irq_dbf;
	}
	axp20x_pek->irq_dbf = regmap_irq_get_virq(axp20x->regmap_irqc,
						  axp20x_pek->irq_dbf);

	axp20x_pek->input = devm_input_allocate_device(&pdev->dev);
	if (!axp20x_pek->input)
		return -ENOMEM;

	idev = axp20x_pek->input;

	idev->name = "axp20x-pek";
	idev->phys = "m1kbd/input2";
	idev->dev.parent = &pdev->dev;

	input_set_capability(idev, EV_KEY, KEY_POWER);

	input_set_drvdata(idev, axp20x_pek);

	error = devm_request_any_context_irq(&pdev->dev, axp20x_pek->irq_dbr,
					  axp20x_pek_irq, 0,
					  "axp20x-pek-dbr", idev);
	if (error < 0) {
		dev_err(axp20x->dev, "Failed to request dbr IRQ#%d: %d\n",
			axp20x_pek->irq_dbr, error);

		return error;
	}

	error = devm_request_any_context_irq(&pdev->dev, axp20x_pek->irq_dbf,
					  axp20x_pek_irq, 0,
					  "axp20x-pek-dbf", idev);
	if (error < 0) {
		dev_err(axp20x->dev, "Failed to request dbf IRQ#%d: %d\n",
			axp20x_pek->irq_dbf, error);
		return error;
	}

	error = device_create_file(&pdev->dev, &axp20x_dev_attr_startup.attr);
	if (error)
		return error;

	error = device_create_file(&pdev->dev, &axp20x_dev_attr_shutdown.attr);
	if (error)
		goto clear_startup_attr;

	error = input_register_device(idev);
	if (error) {
		dev_err(axp20x->dev, "Can't register input device: %d\n",
			error);
		goto clear_attr;
	}

	platform_set_drvdata(pdev, axp20x_pek);

	return 0;

clear_attr:
	device_remove_file(&pdev->dev, &axp20x_dev_attr_shutdown.attr);

clear_startup_attr:
	device_remove_file(&pdev->dev, &axp20x_dev_attr_startup.attr);

	return error;
}

static int axp20x_pek_remove(struct platform_device *pdev)
{
	device_remove_file(&pdev->dev, &axp20x_dev_attr_shutdown.attr);
	device_remove_file(&pdev->dev, &axp20x_dev_attr_startup.attr);

	return 0;
}

static struct platform_driver axp20x_pek_driver = {
	.probe		= axp20x_pek_probe,
	.remove		= axp20x_pek_remove,
	.driver		= {
		.name		= "axp20x-pek",
	},
};
module_platform_driver(axp20x_pek_driver);

MODULE_DESCRIPTION("axp20x Power Button");
MODULE_AUTHOR("Carlo Caione <carlo@caione.org>");
MODULE_LICENSE("GPL");
