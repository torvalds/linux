/*
 * rotary_encoder.c
 *
 * (c) 2009 Daniel Mack <daniel@caiaq.de>
 * Copyright (C) 2011 Johan Hovold <jhovold@gmail.com>
 *
 * state machine code inspired by code from Tim Ruetz
 *
 * A generic driver for rotary encoders connected to GPIO lines.
 * See file:Documentation/input/rotary-encoder.txt for more information
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/interrupt.h>
#include <linux/input.h>
#include <linux/device.h>
#include <linux/platform_device.h>
#include <linux/gpio/consumer.h>
#include <linux/slab.h>
#include <linux/of.h>
#include <linux/pm.h>
#include <linux/property.h>

#define DRV_NAME "rotary-encoder"

struct rotary_encoder {
	struct input_dev *input;

	struct mutex access_mutex;

	u32 steps;
	u32 axis;
	bool relative_axis;
	bool rollover;

	unsigned int pos;

	struct gpio_desc *gpio_a;
	struct gpio_desc *gpio_b;

	unsigned int irq_a;
	unsigned int irq_b;

	bool armed;
	unsigned char dir;	/* 0 - clockwise, 1 - CCW */

	char last_stable;
};

static int rotary_encoder_get_state(struct rotary_encoder *encoder)
{
	int a = !!gpiod_get_value_cansleep(encoder->gpio_a);
	int b = !!gpiod_get_value_cansleep(encoder->gpio_b);

	return ((a << 1) | b);
}

static void rotary_encoder_report_event(struct rotary_encoder *encoder)
{
	if (encoder->relative_axis) {
		input_report_rel(encoder->input,
				 encoder->axis, encoder->dir ? -1 : 1);
	} else {
		unsigned int pos = encoder->pos;

		if (encoder->dir) {
			/* turning counter-clockwise */
			if (encoder->rollover)
				pos += encoder->steps;
			if (pos)
				pos--;
		} else {
			/* turning clockwise */
			if (encoder->rollover || pos < encoder->steps)
				pos++;
		}

		if (encoder->rollover)
			pos %= encoder->steps;

		encoder->pos = pos;
		input_report_abs(encoder->input, encoder->axis, encoder->pos);
	}

	input_sync(encoder->input);
}

static irqreturn_t rotary_encoder_irq(int irq, void *dev_id)
{
	struct rotary_encoder *encoder = dev_id;
	int state;

	mutex_lock(&encoder->access_mutex);

	state = rotary_encoder_get_state(encoder);

	switch (state) {
	case 0x0:
		if (encoder->armed) {
			rotary_encoder_report_event(encoder);
			encoder->armed = false;
		}
		break;

	case 0x1:
	case 0x2:
		if (encoder->armed)
			encoder->dir = state - 1;
		break;

	case 0x3:
		encoder->armed = true;
		break;
	}

	mutex_unlock(&encoder->access_mutex);

	return IRQ_HANDLED;
}

static irqreturn_t rotary_encoder_half_period_irq(int irq, void *dev_id)
{
	struct rotary_encoder *encoder = dev_id;
	int state;

	mutex_lock(&encoder->access_mutex);

	state = rotary_encoder_get_state(encoder);

	switch (state) {
	case 0x00:
	case 0x03:
		if (state != encoder->last_stable) {
			rotary_encoder_report_event(encoder);
			encoder->last_stable = state;
		}
		break;

	case 0x01:
	case 0x02:
		encoder->dir = (encoder->last_stable + state) & 0x01;
		break;
	}

	mutex_unlock(&encoder->access_mutex);

	return IRQ_HANDLED;
}

static irqreturn_t rotary_encoder_quarter_period_irq(int irq, void *dev_id)
{
	struct rotary_encoder *encoder = dev_id;
	unsigned char sum;
	int state;

	mutex_lock(&encoder->access_mutex);

	state = rotary_encoder_get_state(encoder);

	/*
	 * We encode the previous and the current state using a byte.
	 * The previous state in the MSB nibble, the current state in the LSB
	 * nibble. Then use a table to decide the direction of the turn.
	 */
	sum = (encoder->last_stable << 4) + state;
	switch (sum) {
	case 0x31:
	case 0x10:
	case 0x02:
	case 0x23:
		encoder->dir = 0; /* clockwise */
		break;

	case 0x13:
	case 0x01:
	case 0x20:
	case 0x32:
		encoder->dir = 1; /* counter-clockwise */
		break;

	default:
		/*
		 * Ignore all other values. This covers the case when the
		 * state didn't change (a spurious interrupt) and the
		 * cases where the state changed by two steps, making it
		 * impossible to tell the direction.
		 *
		 * In either case, don't report any event and save the
		 * state for later.
		 */
		goto out;
	}

	rotary_encoder_report_event(encoder);

out:
	encoder->last_stable = state;
	mutex_unlock(&encoder->access_mutex);

	return IRQ_HANDLED;
}

static int rotary_encoder_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct rotary_encoder *encoder;
	struct input_dev *input;
	irq_handler_t handler;
	u32 steps_per_period;
	int err;

	encoder = devm_kzalloc(dev, sizeof(struct rotary_encoder), GFP_KERNEL);
	if (!encoder)
		return -ENOMEM;

	mutex_init(&encoder->access_mutex);

	device_property_read_u32(dev, "rotary-encoder,steps", &encoder->steps);

	err = device_property_read_u32(dev, "rotary-encoder,steps-per-period",
				       &steps_per_period);
	if (err) {
		/*
		 * The 'half-period' property has been deprecated, you must
		 * use 'steps-per-period' and set an appropriate value, but
		 * we still need to parse it to maintain compatibility. If
		 * neither property is present we fall back to the one step
		 * per period behavior.
		 */
		steps_per_period = device_property_read_bool(dev,
					"rotary-encoder,half-period") ? 2 : 1;
	}

	encoder->rollover =
		device_property_read_bool(dev, "rotary-encoder,rollover");

	device_property_read_u32(dev, "linux,axis", &encoder->axis);
	encoder->relative_axis =
		device_property_read_bool(dev, "rotary-encoder,relative-axis");

	encoder->gpio_a = devm_gpiod_get_index(dev, NULL, 0, GPIOD_IN);
	if (IS_ERR(encoder->gpio_a)) {
		err = PTR_ERR(encoder->gpio_a);
		dev_err(dev, "unable to get GPIO at index 0: %d\n", err);
		return err;
	}

	encoder->irq_a = gpiod_to_irq(encoder->gpio_a);

	encoder->gpio_b = devm_gpiod_get_index(dev, NULL, 1, GPIOD_IN);
	if (IS_ERR(encoder->gpio_b)) {
		err = PTR_ERR(encoder->gpio_b);
		dev_err(dev, "unable to get GPIO at index 1: %d\n", err);
		return err;
	}

	encoder->irq_b = gpiod_to_irq(encoder->gpio_b);

	input = devm_input_allocate_device(dev);
	if (!input)
		return -ENOMEM;

	encoder->input = input;

	input->name = pdev->name;
	input->id.bustype = BUS_HOST;
	input->dev.parent = dev;

	if (encoder->relative_axis)
		input_set_capability(input, EV_REL, encoder->axis);
	else
		input_set_abs_params(input,
				     encoder->axis, 0, encoder->steps, 0, 1);

	switch (steps_per_period) {
	case 4:
		handler = &rotary_encoder_quarter_period_irq;
		encoder->last_stable = rotary_encoder_get_state(encoder);
		break;
	case 2:
		handler = &rotary_encoder_half_period_irq;
		encoder->last_stable = rotary_encoder_get_state(encoder);
		break;
	case 1:
		handler = &rotary_encoder_irq;
		break;
	default:
		dev_err(dev, "'%d' is not a valid steps-per-period value\n",
			steps_per_period);
		return -EINVAL;
	}

	err = devm_request_threaded_irq(dev, encoder->irq_a, NULL, handler,
				IRQF_TRIGGER_RISING | IRQF_TRIGGER_FALLING |
				IRQF_ONESHOT,
				DRV_NAME, encoder);
	if (err) {
		dev_err(dev, "unable to request IRQ %d\n", encoder->irq_a);
		return err;
	}

	err = devm_request_threaded_irq(dev, encoder->irq_b, NULL, handler,
				IRQF_TRIGGER_RISING | IRQF_TRIGGER_FALLING |
				IRQF_ONESHOT,
				DRV_NAME, encoder);
	if (err) {
		dev_err(dev, "unable to request IRQ %d\n", encoder->irq_b);
		return err;
	}

	err = input_register_device(input);
	if (err) {
		dev_err(dev, "failed to register input device\n");
		return err;
	}

	device_init_wakeup(dev,
			   device_property_read_bool(dev, "wakeup-source"));

	platform_set_drvdata(pdev, encoder);

	return 0;
}

static int __maybe_unused rotary_encoder_suspend(struct device *dev)
{
	struct rotary_encoder *encoder = dev_get_drvdata(dev);

	if (device_may_wakeup(dev)) {
		enable_irq_wake(encoder->irq_a);
		enable_irq_wake(encoder->irq_b);
	}

	return 0;
}

static int __maybe_unused rotary_encoder_resume(struct device *dev)
{
	struct rotary_encoder *encoder = dev_get_drvdata(dev);

	if (device_may_wakeup(dev)) {
		disable_irq_wake(encoder->irq_a);
		disable_irq_wake(encoder->irq_b);
	}

	return 0;
}

static SIMPLE_DEV_PM_OPS(rotary_encoder_pm_ops,
			 rotary_encoder_suspend, rotary_encoder_resume);

#ifdef CONFIG_OF
static const struct of_device_id rotary_encoder_of_match[] = {
	{ .compatible = "rotary-encoder", },
	{ },
};
MODULE_DEVICE_TABLE(of, rotary_encoder_of_match);
#endif

static struct platform_driver rotary_encoder_driver = {
	.probe		= rotary_encoder_probe,
	.driver		= {
		.name	= DRV_NAME,
		.pm	= &rotary_encoder_pm_ops,
		.of_match_table = of_match_ptr(rotary_encoder_of_match),
	}
};
module_platform_driver(rotary_encoder_driver);

MODULE_ALIAS("platform:" DRV_NAME);
MODULE_DESCRIPTION("GPIO rotary encoder driver");
MODULE_AUTHOR("Daniel Mack <daniel@caiaq.de>, Johan Hovold");
MODULE_LICENSE("GPL v2");
