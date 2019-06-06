// SPDX-License-Identifier: GPL-2.0
//
// Copyright (C) 2018 BayLibre SAS
// Author: Bartosz Golaszewski <bgolaszewski@baylibre.com>
//
// ONKEY driver for MAXIM 77650/77651 charger/power-supply.

#include <linux/i2c.h>
#include <linux/input.h>
#include <linux/interrupt.h>
#include <linux/mfd/max77650.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>

#define MAX77650_ONKEY_MODE_MASK	BIT(3)
#define MAX77650_ONKEY_MODE_PUSH	0x00
#define MAX77650_ONKEY_MODE_SLIDE	BIT(3)

struct max77650_onkey {
	struct input_dev *input;
	unsigned int code;
};

static irqreturn_t max77650_onkey_falling(int irq, void *data)
{
	struct max77650_onkey *onkey = data;

	input_report_key(onkey->input, onkey->code, 0);
	input_sync(onkey->input);

	return IRQ_HANDLED;
}

static irqreturn_t max77650_onkey_rising(int irq, void *data)
{
	struct max77650_onkey *onkey = data;

	input_report_key(onkey->input, onkey->code, 1);
	input_sync(onkey->input);

	return IRQ_HANDLED;
}

static int max77650_onkey_probe(struct platform_device *pdev)
{
	int irq_r, irq_f, error, mode;
	struct max77650_onkey *onkey;
	struct device *dev, *parent;
	struct regmap *map;
	unsigned int type;

	dev = &pdev->dev;
	parent = dev->parent;

	map = dev_get_regmap(parent, NULL);
	if (!map)
		return -ENODEV;

	onkey = devm_kzalloc(dev, sizeof(*onkey), GFP_KERNEL);
	if (!onkey)
		return -ENOMEM;

	error = device_property_read_u32(dev, "linux,code", &onkey->code);
	if (error)
		onkey->code = KEY_POWER;

	if (device_property_read_bool(dev, "maxim,onkey-slide")) {
		mode = MAX77650_ONKEY_MODE_SLIDE;
		type = EV_SW;
	} else {
		mode = MAX77650_ONKEY_MODE_PUSH;
		type = EV_KEY;
	}

	error = regmap_update_bits(map, MAX77650_REG_CNFG_GLBL,
				   MAX77650_ONKEY_MODE_MASK, mode);
	if (error)
		return error;

	irq_f = platform_get_irq_byname(pdev, "nEN_F");
	if (irq_f < 0)
		return irq_f;

	irq_r = platform_get_irq_byname(pdev, "nEN_R");
	if (irq_r < 0)
		return irq_r;

	onkey->input = devm_input_allocate_device(dev);
	if (!onkey->input)
		return -ENOMEM;

	onkey->input->name = "max77650_onkey";
	onkey->input->phys = "max77650_onkey/input0";
	onkey->input->id.bustype = BUS_I2C;
	input_set_capability(onkey->input, type, onkey->code);

	error = devm_request_any_context_irq(dev, irq_f, max77650_onkey_falling,
					     IRQF_ONESHOT, "onkey-down", onkey);
	if (error < 0)
		return error;

	error = devm_request_any_context_irq(dev, irq_r, max77650_onkey_rising,
					     IRQF_ONESHOT, "onkey-up", onkey);
	if (error < 0)
		return error;

	return input_register_device(onkey->input);
}

static struct platform_driver max77650_onkey_driver = {
	.driver = {
		.name = "max77650-onkey",
	},
	.probe = max77650_onkey_probe,
};
module_platform_driver(max77650_onkey_driver);

MODULE_DESCRIPTION("MAXIM 77650/77651 ONKEY driver");
MODULE_AUTHOR("Bartosz Golaszewski <bgolaszewski@baylibre.com>");
MODULE_LICENSE("GPL v2");
