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
#include <linux/gpio.h>
#include <linux/rotary_encoder.h>
#include <linux/slab.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/of_gpio.h>
#include <linux/pm.h>

#define DRV_NAME "rotary-encoder"

struct rotary_encoder {
	struct input_dev *input;
	const struct rotary_encoder_platform_data *pdata;

	unsigned int axis;
	unsigned int pos;

	unsigned int irq_a;
	unsigned int irq_b;

	bool armed;
	unsigned char dir;	/* 0 - clockwise, 1 - CCW */

	char last_stable;
};

static int rotary_encoder_get_state(const struct rotary_encoder_platform_data *pdata)
{
	int a = !!gpio_get_value(pdata->gpio_a);
	int b = !!gpio_get_value(pdata->gpio_b);

	a ^= pdata->inverted_a;
	b ^= pdata->inverted_b;

	return ((a << 1) | b);
}

static void rotary_encoder_report_event(struct rotary_encoder *encoder)
{
	const struct rotary_encoder_platform_data *pdata = encoder->pdata;

	if (pdata->relative_axis) {
		input_report_rel(encoder->input,
				 pdata->axis, encoder->dir ? -1 : 1);
	} else {
		unsigned int pos = encoder->pos;

		if (encoder->dir) {
			/* turning counter-clockwise */
			if (pdata->rollover)
				pos += pdata->steps;
			if (pos)
				pos--;
		} else {
			/* turning clockwise */
			if (pdata->rollover || pos < pdata->steps)
				pos++;
		}

		if (pdata->rollover)
			pos %= pdata->steps;

		encoder->pos = pos;
		input_report_abs(encoder->input, pdata->axis, encoder->pos);
	}

	input_sync(encoder->input);
}

static irqreturn_t rotary_encoder_irq(int irq, void *dev_id)
{
	struct rotary_encoder *encoder = dev_id;
	int state;

	state = rotary_encoder_get_state(encoder->pdata);

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

	return IRQ_HANDLED;
}

static irqreturn_t rotary_encoder_half_period_irq(int irq, void *dev_id)
{
	struct rotary_encoder *encoder = dev_id;
	int state;

	state = rotary_encoder_get_state(encoder->pdata);

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

	return IRQ_HANDLED;
}

static irqreturn_t rotary_encoder_quarter_period_irq(int irq, void *dev_id)
{
	struct rotary_encoder *encoder = dev_id;
	unsigned char sum;
	int state;

	state = rotary_encoder_get_state(encoder->pdata);

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
	return IRQ_HANDLED;
}

#ifdef CONFIG_OF
static const struct of_device_id rotary_encoder_of_match[] = {
	{ .compatible = "rotary-encoder", },
	{ },
};
MODULE_DEVICE_TABLE(of, rotary_encoder_of_match);

static struct rotary_encoder_platform_data *rotary_encoder_parse_dt(struct device *dev)
{
	const struct of_device_id *of_id =
				of_match_device(rotary_encoder_of_match, dev);
	struct device_node *np = dev->of_node;
	struct rotary_encoder_platform_data *pdata;
	enum of_gpio_flags flags;
	int error;

	if (!of_id || !np)
		return NULL;

	pdata = kzalloc(sizeof(struct rotary_encoder_platform_data),
			GFP_KERNEL);
	if (!pdata)
		return ERR_PTR(-ENOMEM);

	of_property_read_u32(np, "rotary-encoder,steps", &pdata->steps);
	of_property_read_u32(np, "linux,axis", &pdata->axis);

	pdata->gpio_a = of_get_gpio_flags(np, 0, &flags);
	pdata->inverted_a = flags & OF_GPIO_ACTIVE_LOW;

	pdata->gpio_b = of_get_gpio_flags(np, 1, &flags);
	pdata->inverted_b = flags & OF_GPIO_ACTIVE_LOW;

	pdata->relative_axis =
		of_property_read_bool(np, "rotary-encoder,relative-axis");
	pdata->rollover = of_property_read_bool(np, "rotary-encoder,rollover");

	error = of_property_read_u32(np, "rotary-encoder,steps-per-period",
				     &pdata->steps_per_period);
	if (error) {
		/*
		 * The 'half-period' property has been deprecated, you must use
		 * 'steps-per-period' and set an appropriate value, but we still
		 * need to parse it to maintain compatibility.
		 */
		if (of_property_read_bool(np, "rotary-encoder,half-period")) {
			pdata->steps_per_period = 2;
		} else {
			/* Fallback to one step per period behavior */
			pdata->steps_per_period = 1;
		}
	}

	pdata->wakeup_source = of_property_read_bool(np, "wakeup-source");

	return pdata;
}
#else
static inline struct rotary_encoder_platform_data *
rotary_encoder_parse_dt(struct device *dev)
{
	return NULL;
}
#endif

static int rotary_encoder_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	const struct rotary_encoder_platform_data *pdata = dev_get_platdata(dev);
	struct rotary_encoder *encoder;
	struct input_dev *input;
	irq_handler_t handler;
	int err;

	if (!pdata) {
		pdata = rotary_encoder_parse_dt(dev);
		if (IS_ERR(pdata))
			return PTR_ERR(pdata);

		if (!pdata) {
			dev_err(dev, "missing platform data\n");
			return -EINVAL;
		}
	}

	encoder = kzalloc(sizeof(struct rotary_encoder), GFP_KERNEL);
	input = input_allocate_device();
	if (!encoder || !input) {
		err = -ENOMEM;
		goto exit_free_mem;
	}

	encoder->input = input;
	encoder->pdata = pdata;

	input->name = pdev->name;
	input->id.bustype = BUS_HOST;
	input->dev.parent = dev;

	if (pdata->relative_axis) {
		input->evbit[0] = BIT_MASK(EV_REL);
		input->relbit[0] = BIT_MASK(pdata->axis);
	} else {
		input->evbit[0] = BIT_MASK(EV_ABS);
		input_set_abs_params(encoder->input,
				     pdata->axis, 0, pdata->steps, 0, 1);
	}

	/* request the GPIOs */
	err = gpio_request_one(pdata->gpio_a, GPIOF_IN, dev_name(dev));
	if (err) {
		dev_err(dev, "unable to request GPIO %d\n", pdata->gpio_a);
		goto exit_free_mem;
	}

	err = gpio_request_one(pdata->gpio_b, GPIOF_IN, dev_name(dev));
	if (err) {
		dev_err(dev, "unable to request GPIO %d\n", pdata->gpio_b);
		goto exit_free_gpio_a;
	}

	encoder->irq_a = gpio_to_irq(pdata->gpio_a);
	encoder->irq_b = gpio_to_irq(pdata->gpio_b);

	switch (pdata->steps_per_period) {
	case 4:
		handler = &rotary_encoder_quarter_period_irq;
		encoder->last_stable = rotary_encoder_get_state(pdata);
		break;
	case 2:
		handler = &rotary_encoder_half_period_irq;
		encoder->last_stable = rotary_encoder_get_state(pdata);
		break;
	case 1:
		handler = &rotary_encoder_irq;
		break;
	default:
		dev_err(dev, "'%d' is not a valid steps-per-period value\n",
			pdata->steps_per_period);
		err = -EINVAL;
		goto exit_free_gpio_b;
	}

	err = request_irq(encoder->irq_a, handler,
			  IRQF_TRIGGER_RISING | IRQF_TRIGGER_FALLING,
			  DRV_NAME, encoder);
	if (err) {
		dev_err(dev, "unable to request IRQ %d\n", encoder->irq_a);
		goto exit_free_gpio_b;
	}

	err = request_irq(encoder->irq_b, handler,
			  IRQF_TRIGGER_RISING | IRQF_TRIGGER_FALLING,
			  DRV_NAME, encoder);
	if (err) {
		dev_err(dev, "unable to request IRQ %d\n", encoder->irq_b);
		goto exit_free_irq_a;
	}

	err = input_register_device(input);
	if (err) {
		dev_err(dev, "failed to register input device\n");
		goto exit_free_irq_b;
	}

	device_init_wakeup(&pdev->dev, pdata->wakeup_source);

	platform_set_drvdata(pdev, encoder);

	return 0;

exit_free_irq_b:
	free_irq(encoder->irq_b, encoder);
exit_free_irq_a:
	free_irq(encoder->irq_a, encoder);
exit_free_gpio_b:
	gpio_free(pdata->gpio_b);
exit_free_gpio_a:
	gpio_free(pdata->gpio_a);
exit_free_mem:
	input_free_device(input);
	kfree(encoder);
	if (!dev_get_platdata(&pdev->dev))
		kfree(pdata);

	return err;
}

static int rotary_encoder_remove(struct platform_device *pdev)
{
	struct rotary_encoder *encoder = platform_get_drvdata(pdev);
	const struct rotary_encoder_platform_data *pdata = encoder->pdata;

	device_init_wakeup(&pdev->dev, false);

	free_irq(encoder->irq_a, encoder);
	free_irq(encoder->irq_b, encoder);
	gpio_free(pdata->gpio_a);
	gpio_free(pdata->gpio_b);

	input_unregister_device(encoder->input);
	kfree(encoder);

	if (!dev_get_platdata(&pdev->dev))
		kfree(pdata);

	return 0;
}

#ifdef CONFIG_PM_SLEEP
static int rotary_encoder_suspend(struct device *dev)
{
	struct rotary_encoder *encoder = dev_get_drvdata(dev);

	if (device_may_wakeup(dev)) {
		enable_irq_wake(encoder->irq_a);
		enable_irq_wake(encoder->irq_b);
	}

	return 0;
}

static int rotary_encoder_resume(struct device *dev)
{
	struct rotary_encoder *encoder = dev_get_drvdata(dev);

	if (device_may_wakeup(dev)) {
		disable_irq_wake(encoder->irq_a);
		disable_irq_wake(encoder->irq_b);
	}

	return 0;
}
#endif

static SIMPLE_DEV_PM_OPS(rotary_encoder_pm_ops,
		 rotary_encoder_suspend, rotary_encoder_resume);

static struct platform_driver rotary_encoder_driver = {
	.probe		= rotary_encoder_probe,
	.remove		= rotary_encoder_remove,
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
