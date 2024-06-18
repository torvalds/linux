// SPDX-License-Identifier: GPL-2.0-only
/*
 * Texas Instruments' TPS65217 and TPS65218 Power Button Input Driver
 *
 * Copyright (C) 2014 Texas Instruments Incorporated - http://www.ti.com/
 * Author: Felipe Balbi <balbi@ti.com>
 * Author: Marcin Niestroj <m.niestroj@grinn-global.com>
 */

#include <linux/init.h>
#include <linux/input.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/mfd/tps65217.h>
#include <linux/mfd/tps65218.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/slab.h>

struct tps6521x_data {
	unsigned int reg_status;
	unsigned int pb_mask;
	const char *name;
};

static const struct tps6521x_data tps65217_data = {
	.reg_status = TPS65217_REG_STATUS,
	.pb_mask = TPS65217_STATUS_PB,
	.name = "tps65217_pwrbutton",
};

static const struct tps6521x_data tps65218_data = {
	.reg_status = TPS65218_REG_STATUS,
	.pb_mask = TPS65218_STATUS_PB_STATE,
	.name = "tps65218_pwrbutton",
};

struct tps6521x_pwrbutton {
	struct device *dev;
	struct regmap *regmap;
	struct input_dev *idev;
	const struct tps6521x_data *data;
	char phys[32];
};

static const struct of_device_id of_tps6521x_pb_match[] = {
	{ .compatible = "ti,tps65217-pwrbutton", .data = &tps65217_data },
	{ .compatible = "ti,tps65218-pwrbutton", .data = &tps65218_data },
	{ },
};
MODULE_DEVICE_TABLE(of, of_tps6521x_pb_match);

static irqreturn_t tps6521x_pb_irq(int irq, void *_pwr)
{
	struct tps6521x_pwrbutton *pwr = _pwr;
	const struct tps6521x_data *tps_data = pwr->data;
	unsigned int reg;
	int error;

	error = regmap_read(pwr->regmap, tps_data->reg_status, &reg);
	if (error) {
		dev_err(pwr->dev, "can't read register: %d\n", error);
		goto out;
	}

	if (reg & tps_data->pb_mask) {
		input_report_key(pwr->idev, KEY_POWER, 1);
		pm_wakeup_event(pwr->dev, 0);
	} else {
		input_report_key(pwr->idev, KEY_POWER, 0);
	}

	input_sync(pwr->idev);

out:
	return IRQ_HANDLED;
}

static int tps6521x_pb_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct tps6521x_pwrbutton *pwr;
	struct input_dev *idev;
	const struct of_device_id *match;
	int error;
	int irq;

	match = of_match_node(of_tps6521x_pb_match, dev->of_node);
	if (!match)
		return -ENXIO;

	pwr = devm_kzalloc(dev, sizeof(*pwr), GFP_KERNEL);
	if (!pwr)
		return -ENOMEM;

	pwr->data = match->data;

	idev = devm_input_allocate_device(dev);
	if (!idev)
		return -ENOMEM;

	idev->name = pwr->data->name;
	snprintf(pwr->phys, sizeof(pwr->phys), "%s/input0",
		pwr->data->name);
	idev->phys = pwr->phys;
	idev->dev.parent = dev;
	idev->id.bustype = BUS_I2C;

	input_set_capability(idev, EV_KEY, KEY_POWER);

	pwr->regmap = dev_get_regmap(dev->parent, NULL);
	pwr->dev = dev;
	pwr->idev = idev;
	device_init_wakeup(dev, true);

	irq = platform_get_irq(pdev, 0);
	if (irq < 0)
		return -EINVAL;

	error = devm_request_threaded_irq(dev, irq, NULL, tps6521x_pb_irq,
					  IRQF_TRIGGER_RISING |
						IRQF_TRIGGER_FALLING |
						IRQF_ONESHOT,
					  pwr->data->name, pwr);
	if (error) {
		dev_err(dev, "failed to request IRQ #%d: %d\n", irq, error);
		return error;
	}

	error= input_register_device(idev);
	if (error) {
		dev_err(dev, "Can't register power button: %d\n", error);
		return error;
	}

	return 0;
}

static const struct platform_device_id tps6521x_pwrbtn_id_table[] = {
	{ "tps65218-pwrbutton", },
	{ "tps65217-pwrbutton", },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(platform, tps6521x_pwrbtn_id_table);

static struct platform_driver tps6521x_pb_driver = {
	.probe	= tps6521x_pb_probe,
	.driver	= {
		.name	= "tps6521x_pwrbutton",
		.of_match_table = of_tps6521x_pb_match,
	},
	.id_table = tps6521x_pwrbtn_id_table,
};
module_platform_driver(tps6521x_pb_driver);

MODULE_DESCRIPTION("TPS6521X Power Button");
MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Felipe Balbi <balbi@ti.com>");
