// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2025 André Apitzsch <git@apitzsch.eu>
 */

#include <linux/input.h>
#include <linux/property.h>
#include "rmi_driver.h"

struct f1a_data {
	struct input_dev *input;

	u32 *keymap;
	unsigned int num_keys;
};

static int rmi_f1a_parse_device_properties(struct rmi_function *fn, struct f1a_data *f1a)
{
	static const char buttons_property[] = "linux,keycodes";
	struct device *dev = &fn->dev;
	u32 *buttonmap;
	int n_keys;
	int error;

	if (!device_property_present(dev, buttons_property))
		return 0;

	n_keys = device_property_count_u32(dev, buttons_property);
	if (n_keys <= 0) {
		error = n_keys < 0 ? n_keys : -EINVAL;
		dev_err(dev, "Invalid/malformed '%s' property: %d\n",
			buttons_property, error);
		return error;
	}

	buttonmap = devm_kmalloc_array(dev, n_keys, sizeof(*buttonmap),
				       GFP_KERNEL);
	if (!buttonmap)
		return -ENOMEM;

	error = device_property_read_u32_array(dev, buttons_property,
					       buttonmap, n_keys);
	if (error) {
		dev_err(dev, "Failed to parse '%s' property: %d\n",
			buttons_property, error);
		return error;
	}

	f1a->keymap = buttonmap;
	f1a->num_keys = n_keys;

	return 0;
}

static irqreturn_t rmi_f1a_attention(int irq, void *ctx)
{
	struct rmi_function *fn = ctx;
	struct f1a_data *f1a = dev_get_drvdata(&fn->dev);
	char button_bitmask;
	int key;
	int error;

	error = rmi_read_block(fn->rmi_dev, fn->fd.data_base_addr,
			       &button_bitmask, sizeof(button_bitmask));
	if (error) {
		dev_err(&fn->dev, "Failed to read object data. Code: %d.\n",
			error);
		return IRQ_RETVAL(error);
	}

	for (key = 0; key < f1a->num_keys; key++)
		input_report_key(f1a->input, f1a->keymap[key],
				 button_bitmask & BIT(key));

	return IRQ_HANDLED;
}

static int rmi_f1a_config(struct rmi_function *fn)
{
	struct f1a_data *f1a = dev_get_drvdata(&fn->dev);
	struct rmi_driver *drv = fn->rmi_dev->driver;

	if (f1a->num_keys)
		drv->set_irq_bits(fn->rmi_dev, fn->irq_mask);

	return 0;
}

static int rmi_f1a_initialize(struct rmi_function *fn, struct f1a_data *f1a)
{
	int error;
	int i;

	error = rmi_f1a_parse_device_properties(fn, f1a);
	if (error)
		return error;

	for (i = 0; i < f1a->num_keys; i++)
		input_set_capability(f1a->input, EV_KEY, f1a->keymap[i]);

	f1a->input->keycode = f1a->keymap;
	f1a->input->keycodemax = f1a->num_keys;
	f1a->input->keycodesize = sizeof(f1a->keymap[0]);

	return 0;
}

static int rmi_f1a_probe(struct rmi_function *fn)
{
	struct rmi_device *rmi_dev = fn->rmi_dev;
	struct rmi_driver_data *drv_data = dev_get_drvdata(&rmi_dev->dev);
	struct f1a_data *f1a;
	int error;

	if (!drv_data->input) {
		dev_info(&fn->dev, "F1A: no input device found, ignoring\n");
		return -ENXIO;
	}

	f1a = devm_kzalloc(&fn->dev, sizeof(*f1a), GFP_KERNEL);
	if (!f1a)
		return -ENOMEM;

	f1a->input = drv_data->input;

	error = rmi_f1a_initialize(fn, f1a);
	if (error)
		return error;

	dev_set_drvdata(&fn->dev, f1a);

	return 0;
}

struct rmi_function_handler rmi_f1a_handler = {
	.driver = {
		.name = "rmi4_f1a",
	},
	.func = 0x1a,
	.probe = rmi_f1a_probe,
	.config = rmi_f1a_config,
	.attention = rmi_f1a_attention,
};
