// SPDX-License-Identifier: GPL-2.0
/*
 * power button driver for TI TPS6594 PMICs
 *
 * Copyright (C) 2025 Critical Link LLC - https://www.criticallink.com/
 */
#include <linux/init.h>
#include <linux/input.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/mfd/tps6594.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/slab.h>

struct tps6594_pwrbutton {
	struct device *dev;
	struct input_dev *idev;
	char phys[32];
};

static irqreturn_t tps6594_pb_push_irq(int irq, void *_pwr)
{
	struct tps6594_pwrbutton *pwr = _pwr;

	input_report_key(pwr->idev, KEY_POWER, 1);
	pm_wakeup_event(pwr->dev, 0);
	input_sync(pwr->idev);

	return IRQ_HANDLED;
}

static irqreturn_t tps6594_pb_release_irq(int irq, void *_pwr)
{
	struct tps6594_pwrbutton *pwr = _pwr;

	input_report_key(pwr->idev, KEY_POWER, 0);
	input_sync(pwr->idev);

	return IRQ_HANDLED;
}

static int tps6594_pb_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct tps6594_pwrbutton *pwr;
	struct input_dev *idev;
	int error;
	int push_irq;
	int release_irq;

	pwr = devm_kzalloc(dev, sizeof(*pwr), GFP_KERNEL);
	if (!pwr)
		return -ENOMEM;

	idev = devm_input_allocate_device(dev);
	if (!idev)
		return -ENOMEM;

	idev->name = pdev->name;
	snprintf(pwr->phys, sizeof(pwr->phys), "%s/input0",
		 pdev->name);
	idev->phys = pwr->phys;
	idev->id.bustype = BUS_I2C;

	input_set_capability(idev, EV_KEY, KEY_POWER);

	pwr->dev = dev;
	pwr->idev = idev;
	device_init_wakeup(dev, true);

	push_irq = platform_get_irq(pdev, 0);
	if (push_irq < 0)
		return -EINVAL;

	release_irq = platform_get_irq(pdev, 1);
	if (release_irq < 0)
		return -EINVAL;

	error = devm_request_threaded_irq(dev, push_irq, NULL,
					  tps6594_pb_push_irq,
					  IRQF_ONESHOT,
					  pdev->resource[0].name, pwr);
	if (error) {
		dev_err(dev, "failed to request push IRQ #%d: %d\n", push_irq,
			error);
		return error;
	}

	error = devm_request_threaded_irq(dev, release_irq, NULL,
					  tps6594_pb_release_irq,
					  IRQF_ONESHOT,
					  pdev->resource[1].name, pwr);
	if (error) {
		dev_err(dev, "failed to request release IRQ #%d: %d\n",
			release_irq, error);
		return error;
	}

	error = input_register_device(idev);
	if (error) {
		dev_err(dev, "Can't register power button: %d\n", error);
		return error;
	}

	return 0;
}

static const struct platform_device_id tps6594_pwrbtn_id_table[] = {
	{ "tps6594-pwrbutton", },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(platform, tps6594_pwrbtn_id_table);

static struct platform_driver tps6594_pb_driver = {
	.probe = tps6594_pb_probe,
	.driver = {
		.name = "tps6594_pwrbutton",
	},
	.id_table = tps6594_pwrbtn_id_table,
};
module_platform_driver(tps6594_pb_driver);

MODULE_DESCRIPTION("TPS6594 Power Button");
MODULE_LICENSE("GPL");
