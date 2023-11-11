// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2012-2020 Synaptics Incorporated
 */

#include <linux/kernel.h>
#include <linux/rmi.h>
#include <linux/input.h>
#include <linux/slab.h>
#include "rmi_driver.h"

#define RMI_F3A_MAX_GPIO_COUNT		128
#define RMI_F3A_MAX_REG_SIZE		DIV_ROUND_UP(RMI_F3A_MAX_GPIO_COUNT, 8)

/* Defs for Query 0 */
#define RMI_F3A_GPIO_COUNT		0x7F

#define RMI_F3A_DATA_REGS_MAX_SIZE	RMI_F3A_MAX_REG_SIZE

#define TRACKSTICK_RANGE_START		3
#define TRACKSTICK_RANGE_END		6

struct f3a_data {
	/* Query Data */
	u8 gpio_count;

	u8 register_count;

	u8 data_regs[RMI_F3A_DATA_REGS_MAX_SIZE];
	u16 *gpio_key_map;

	struct input_dev *input;

	struct rmi_function *f03;
	bool trackstick_buttons;
};

static void rmi_f3a_report_button(struct rmi_function *fn,
				  struct f3a_data *f3a, unsigned int button)
{
	u16 key_code = f3a->gpio_key_map[button];
	bool key_down = !(f3a->data_regs[0] & BIT(button));

	if (f3a->trackstick_buttons &&
		button >= TRACKSTICK_RANGE_START &&
		button <= TRACKSTICK_RANGE_END) {
		rmi_f03_overwrite_button(f3a->f03, key_code, key_down);
	} else {
		rmi_dbg(RMI_DEBUG_FN, &fn->dev,
			"%s: call input report key (0x%04x) value (0x%02x)",
			__func__, key_code, key_down);
		input_report_key(f3a->input, key_code, key_down);
	}
}

static irqreturn_t rmi_f3a_attention(int irq, void *ctx)
{
	struct rmi_function *fn = ctx;
	struct f3a_data *f3a = dev_get_drvdata(&fn->dev);
	struct rmi_driver_data *drvdata = dev_get_drvdata(&fn->rmi_dev->dev);
	int error;
	int i;

	if (drvdata->attn_data.data) {
		if (drvdata->attn_data.size < f3a->register_count) {
			dev_warn(&fn->dev,
				 "F3A interrupted, but data is missing\n");
			return IRQ_HANDLED;
		}
		memcpy(f3a->data_regs, drvdata->attn_data.data,
			f3a->register_count);
		drvdata->attn_data.data += f3a->register_count;
		drvdata->attn_data.size -= f3a->register_count;
	} else {
		error = rmi_read_block(fn->rmi_dev, fn->fd.data_base_addr,
					f3a->data_regs, f3a->register_count);
		if (error) {
			dev_err(&fn->dev,
				"%s: Failed to read F3a data registers: %d\n",
				__func__, error);
			return IRQ_RETVAL(error);
		}
	}

	for (i = 0; i < f3a->gpio_count; i++)
		if (f3a->gpio_key_map[i] != KEY_RESERVED)
			rmi_f3a_report_button(fn, f3a, i);
	if (f3a->trackstick_buttons)
		rmi_f03_commit_buttons(f3a->f03);

	return IRQ_HANDLED;
}

static int rmi_f3a_config(struct rmi_function *fn)
{
	struct f3a_data *f3a = dev_get_drvdata(&fn->dev);
	struct rmi_driver *drv = fn->rmi_dev->driver;
	const struct rmi_device_platform_data *pdata =
			rmi_get_platform_data(fn->rmi_dev);

	if (!f3a)
		return 0;

	if (pdata->gpio_data.trackstick_buttons) {
		/* Try [re-]establish link to F03. */
		f3a->f03 = rmi_find_function(fn->rmi_dev, 0x03);
		f3a->trackstick_buttons = f3a->f03 != NULL;
	}

	drv->set_irq_bits(fn->rmi_dev, fn->irq_mask);

	return 0;
}

static bool rmi_f3a_is_valid_button(int button, struct f3a_data *f3a,
					u8 *query1_regs, u8 *ctrl1_regs)
{
	/* gpio exist && direction input */
	return (query1_regs[0] & BIT(button)) && !(ctrl1_regs[0] & BIT(button));
}

static int rmi_f3a_map_gpios(struct rmi_function *fn, struct f3a_data *f3a,
				u8 *query1_regs, u8 *ctrl1_regs)
{
	const struct rmi_device_platform_data *pdata =
			rmi_get_platform_data(fn->rmi_dev);
	struct input_dev *input = f3a->input;
	unsigned int button = BTN_LEFT;
	unsigned int trackstick_button = BTN_LEFT;
	bool button_mapped = false;
	int i;
	int button_count = min_t(u8, f3a->gpio_count, TRACKSTICK_RANGE_END);

	f3a->gpio_key_map = devm_kcalloc(&fn->dev,
						button_count,
						sizeof(f3a->gpio_key_map[0]),
						GFP_KERNEL);
	if (!f3a->gpio_key_map) {
		dev_err(&fn->dev, "Failed to allocate gpio map memory.\n");
		return -ENOMEM;
	}

	for (i = 0; i < button_count; i++) {
		if (!rmi_f3a_is_valid_button(i, f3a, query1_regs, ctrl1_regs))
			continue;

		if (pdata->gpio_data.trackstick_buttons &&
			i >= TRACKSTICK_RANGE_START &&
			i < TRACKSTICK_RANGE_END) {
			f3a->gpio_key_map[i] = trackstick_button++;
		} else if (!pdata->gpio_data.buttonpad || !button_mapped) {
			f3a->gpio_key_map[i] = button;
			input_set_capability(input, EV_KEY, button++);
			button_mapped = true;
		}
	}
	input->keycode = f3a->gpio_key_map;
	input->keycodesize = sizeof(f3a->gpio_key_map[0]);
	input->keycodemax = f3a->gpio_count;

	if (pdata->gpio_data.buttonpad || (button - BTN_LEFT == 1))
		__set_bit(INPUT_PROP_BUTTONPAD, input->propbit);

	return 0;
}

static int rmi_f3a_initialize(struct rmi_function *fn, struct f3a_data *f3a)
{
	u8 query1[RMI_F3A_MAX_REG_SIZE];
	u8 ctrl1[RMI_F3A_MAX_REG_SIZE];
	u8 buf;
	int error;

	error = rmi_read(fn->rmi_dev, fn->fd.query_base_addr, &buf);
	if (error < 0) {
		dev_err(&fn->dev, "Failed to read general info register: %d\n",
			error);
		return -ENODEV;
	}

	f3a->gpio_count = buf & RMI_F3A_GPIO_COUNT;
	f3a->register_count = DIV_ROUND_UP(f3a->gpio_count, 8);

	/* Query1 -> gpio exist */
	error = rmi_read_block(fn->rmi_dev, fn->fd.query_base_addr + 1,
				query1, f3a->register_count);
	if (error) {
		dev_err(&fn->dev, "Failed to read query1 register\n");
		return error;
	}

	/* Ctrl1 -> gpio direction */
	error = rmi_read_block(fn->rmi_dev, fn->fd.control_base_addr + 1,
				ctrl1, f3a->register_count);
	if (error) {
		dev_err(&fn->dev, "Failed to read control1 register\n");
		return error;
	}

	error = rmi_f3a_map_gpios(fn, f3a, query1, ctrl1);
	if (error)
		return error;

	return 0;
}

static int rmi_f3a_probe(struct rmi_function *fn)
{
	struct rmi_device *rmi_dev = fn->rmi_dev;
	struct rmi_driver_data *drv_data = dev_get_drvdata(&rmi_dev->dev);
	struct f3a_data *f3a;
	int error;

	if (!drv_data->input) {
		dev_info(&fn->dev, "F3A: no input device found, ignoring\n");
		return -ENXIO;
	}

	f3a = devm_kzalloc(&fn->dev, sizeof(*f3a), GFP_KERNEL);
	if (!f3a)
		return -ENOMEM;

	f3a->input = drv_data->input;

	error = rmi_f3a_initialize(fn, f3a);
	if (error)
		return error;

	dev_set_drvdata(&fn->dev, f3a);
	return 0;
}

struct rmi_function_handler rmi_f3a_handler = {
	.driver = {
		.name = "rmi4_f3a",
	},
	.func = 0x3a,
	.probe = rmi_f3a_probe,
	.config = rmi_f3a_config,
	.attention = rmi_f3a_attention,
};
