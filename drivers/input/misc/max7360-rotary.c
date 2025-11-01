// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright 2025 Bootlin
 *
 * Author: Mathieu Dubois-Briand <mathieu.dubois-briand@bootlin.com>
 */

#include <linux/bitfield.h>
#include <linux/device/devres.h>
#include <linux/dev_printk.h>
#include <linux/init.h>
#include <linux/input.h>
#include <linux/interrupt.h>
#include <linux/mfd/max7360.h>
#include <linux/property.h>
#include <linux/platform_device.h>
#include <linux/pm_wakeirq.h>
#include <linux/regmap.h>
#include <linux/types.h>

#define MAX7360_ROTARY_DEFAULT_STEPS 24

struct max7360_rotary {
	struct input_dev *input;
	struct regmap *regmap;
	unsigned int debounce_ms;

	unsigned int pos;

	u32 steps;
	u32 axis;
	bool relative_axis;
	bool rollover;
};

static void max7360_rotary_report_event(struct max7360_rotary *max7360_rotary, int steps)
{
	if (max7360_rotary->relative_axis) {
		input_report_rel(max7360_rotary->input, max7360_rotary->axis, steps);
	} else {
		int pos = max7360_rotary->pos;
		int maxval = max7360_rotary->steps;

		/*
		 * Add steps to the position.
		 * Make sure added steps are always in ]-maxval; maxval[
		 * interval, so (pos + maxval) is always >= 0.
		 * Then set back pos to the [0; maxval[ interval.
		 */
		pos += steps % maxval;
		if (max7360_rotary->rollover)
			pos = (pos + maxval) % maxval;
		else
			pos = clamp(pos, 0, maxval - 1);

		max7360_rotary->pos = pos;
		input_report_abs(max7360_rotary->input, max7360_rotary->axis, max7360_rotary->pos);
	}

	input_sync(max7360_rotary->input);
}

static irqreturn_t max7360_rotary_irq(int irq, void *data)
{
	struct max7360_rotary *max7360_rotary = data;
	struct device *dev = max7360_rotary->input->dev.parent;
	unsigned int val;
	int error;

	error = regmap_read(max7360_rotary->regmap, MAX7360_REG_RTR_CNT, &val);
	if (error < 0) {
		dev_err(dev, "Failed to read rotary counter\n");
		return IRQ_NONE;
	}

	if (val == 0)
		return IRQ_NONE;

	max7360_rotary_report_event(max7360_rotary, sign_extend32(val, 7));

	return IRQ_HANDLED;
}

static int max7360_rotary_hw_init(struct max7360_rotary *max7360_rotary)
{
	struct device *dev = max7360_rotary->input->dev.parent;
	int val;
	int error;

	val = FIELD_PREP(MAX7360_ROT_DEBOUNCE, max7360_rotary->debounce_ms) |
	      FIELD_PREP(MAX7360_ROT_INTCNT, 1) | MAX7360_ROT_INTCNT_DLY;
	error = regmap_write(max7360_rotary->regmap, MAX7360_REG_RTRCFG, val);
	if (error)
		dev_err(dev, "Failed to set max7360 rotary encoder configuration\n");

	return error;
}

static int max7360_rotary_probe(struct platform_device *pdev)
{
	struct max7360_rotary *max7360_rotary;
	struct device *dev = &pdev->dev;
	struct input_dev *input;
	struct regmap *regmap;
	int irq;
	int error;

	regmap = dev_get_regmap(dev->parent, NULL);
	if (!regmap)
		return dev_err_probe(dev, -ENODEV, "Could not get parent regmap\n");

	irq = fwnode_irq_get_byname(dev_fwnode(dev->parent), "inti");
	if (irq < 0)
		return dev_err_probe(dev, irq, "Failed to get IRQ\n");

	max7360_rotary = devm_kzalloc(dev, sizeof(*max7360_rotary), GFP_KERNEL);
	if (!max7360_rotary)
		return -ENOMEM;

	max7360_rotary->regmap = regmap;

	device_property_read_u32(dev->parent, "linux,axis", &max7360_rotary->axis);
	max7360_rotary->rollover = device_property_read_bool(dev->parent,
							     "rotary-encoder,rollover");
	max7360_rotary->relative_axis =
		device_property_read_bool(dev->parent, "rotary-encoder,relative-axis");

	error = device_property_read_u32(dev->parent, "rotary-encoder,steps",
					 &max7360_rotary->steps);
	if (error)
		max7360_rotary->steps = MAX7360_ROTARY_DEFAULT_STEPS;

	device_property_read_u32(dev->parent, "rotary-debounce-delay-ms",
				 &max7360_rotary->debounce_ms);
	if (max7360_rotary->debounce_ms > MAX7360_ROT_DEBOUNCE_MAX)
		return dev_err_probe(dev, -EINVAL, "Invalid debounce timing: %u\n",
				     max7360_rotary->debounce_ms);

	input = devm_input_allocate_device(dev);
	if (!input)
		return -ENOMEM;

	max7360_rotary->input = input;

	input->id.bustype = BUS_I2C;
	input->name = pdev->name;

	if (max7360_rotary->relative_axis)
		input_set_capability(input, EV_REL, max7360_rotary->axis);
	else
		input_set_abs_params(input, max7360_rotary->axis, 0, max7360_rotary->steps, 0, 1);

	error = devm_request_threaded_irq(dev, irq, NULL, max7360_rotary_irq,
					  IRQF_ONESHOT | IRQF_SHARED,
					  "max7360-rotary", max7360_rotary);
	if (error)
		return dev_err_probe(dev, error, "Failed to register interrupt\n");

	error = input_register_device(input);
	if (error)
		return dev_err_probe(dev, error, "Could not register input device\n");

	error = max7360_rotary_hw_init(max7360_rotary);
	if (error)
		return dev_err_probe(dev, error, "Failed to initialize max7360 rotary\n");

	device_init_wakeup(dev, true);
	error = dev_pm_set_wake_irq(dev, irq);
	if (error)
		dev_warn(dev, "Failed to set up wakeup irq: %d\n", error);

	return 0;
}

static void max7360_rotary_remove(struct platform_device *pdev)
{
	dev_pm_clear_wake_irq(&pdev->dev);
	device_init_wakeup(&pdev->dev, false);
}

static struct platform_driver max7360_rotary_driver = {
	.driver = {
		.name	= "max7360-rotary",
	},
	.probe		= max7360_rotary_probe,
	.remove		= max7360_rotary_remove,
};
module_platform_driver(max7360_rotary_driver);

MODULE_DESCRIPTION("MAX7360 Rotary driver");
MODULE_AUTHOR("Mathieu Dubois-Briand <mathieu.dubois-briand@bootlin.com>");
MODULE_LICENSE("GPL");
