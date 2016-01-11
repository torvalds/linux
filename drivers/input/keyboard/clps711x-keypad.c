/*
 * Cirrus Logic CLPS711X Keypad driver
 *
 * Copyright (C) 2014 Alexander Shiyan <shc_work@mail.ru>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <linux/input.h>
#include <linux/input-polldev.h>
#include <linux/module.h>
#include <linux/of_gpio.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/sched.h>
#include <linux/input/matrix_keypad.h>
#include <linux/mfd/syscon.h>
#include <linux/mfd/syscon/clps711x.h>

#define CLPS711X_KEYPAD_COL_COUNT	8

struct clps711x_gpio_data {
	struct gpio_desc *desc;
	DECLARE_BITMAP(last_state, CLPS711X_KEYPAD_COL_COUNT);
};

struct clps711x_keypad_data {
	struct regmap			*syscon;
	int				row_count;
	unsigned int			row_shift;
	struct clps711x_gpio_data	*gpio_data;
};

static void clps711x_keypad_poll(struct input_polled_dev *dev)
{
	const unsigned short *keycodes = dev->input->keycode;
	struct clps711x_keypad_data *priv = dev->private;
	bool sync = false;
	int col, row;

	for (col = 0; col < CLPS711X_KEYPAD_COL_COUNT; col++) {
		/* Assert column */
		regmap_update_bits(priv->syscon, SYSCON_OFFSET,
				   SYSCON1_KBDSCAN_MASK,
				   SYSCON1_KBDSCAN(8 + col));

		/* Scan rows */
		for (row = 0; row < priv->row_count; row++) {
			struct clps711x_gpio_data *data = &priv->gpio_data[row];
			bool state, state1;

			/* Read twice for protection against fluctuations */
			do {
				state = gpiod_get_value_cansleep(data->desc);
				cond_resched();
				state1 = gpiod_get_value_cansleep(data->desc);
			} while (state != state1);

			if (test_bit(col, data->last_state) != state) {
				int code = MATRIX_SCAN_CODE(row, col,
							    priv->row_shift);

				if (state) {
					set_bit(col, data->last_state);
					input_event(dev->input, EV_MSC,
						    MSC_SCAN, code);
				} else {
					clear_bit(col, data->last_state);
				}

				if (keycodes[code])
					input_report_key(dev->input,
							 keycodes[code], state);
				sync = true;
			}
		}

		/* Set all columns to low */
		regmap_update_bits(priv->syscon, SYSCON_OFFSET,
				   SYSCON1_KBDSCAN_MASK, SYSCON1_KBDSCAN(1));
	}

	if (sync)
		input_sync(dev->input);
}

static int clps711x_keypad_probe(struct platform_device *pdev)
{
	struct clps711x_keypad_data *priv;
	struct device *dev = &pdev->dev;
	struct device_node *np = dev->of_node;
	struct input_polled_dev *poll_dev;
	u32 poll_interval;
	int i, err;

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->syscon =
		syscon_regmap_lookup_by_compatible("cirrus,clps711x-syscon1");
	if (IS_ERR(priv->syscon))
		return PTR_ERR(priv->syscon);

	priv->row_count = of_gpio_named_count(np, "row-gpios");
	if (priv->row_count < 1)
		return -EINVAL;

	priv->gpio_data = devm_kzalloc(dev,
				sizeof(*priv->gpio_data) * priv->row_count,
				GFP_KERNEL);
	if (!priv->gpio_data)
		return -ENOMEM;

	priv->row_shift = get_count_order(CLPS711X_KEYPAD_COL_COUNT);

	for (i = 0; i < priv->row_count; i++) {
		struct clps711x_gpio_data *data = &priv->gpio_data[i];

		data->desc = devm_gpiod_get_index(dev, "row", i, GPIOD_IN);
		if (IS_ERR(data->desc))
			return PTR_ERR(data->desc);
	}

	err = of_property_read_u32(np, "poll-interval", &poll_interval);
	if (err)
		return err;

	poll_dev = input_allocate_polled_device();
	if (!poll_dev)
		return -ENOMEM;

	poll_dev->private		= priv;
	poll_dev->poll			= clps711x_keypad_poll;
	poll_dev->poll_interval		= poll_interval;
	poll_dev->input->name		= pdev->name;
	poll_dev->input->dev.parent	= dev;
	poll_dev->input->id.bustype	= BUS_HOST;
	poll_dev->input->id.vendor	= 0x0001;
	poll_dev->input->id.product	= 0x0001;
	poll_dev->input->id.version	= 0x0100;

	err = matrix_keypad_build_keymap(NULL, NULL, priv->row_count,
					 CLPS711X_KEYPAD_COL_COUNT,
					 NULL, poll_dev->input);
	if (err)
		goto out_err;

	input_set_capability(poll_dev->input, EV_MSC, MSC_SCAN);
	if (of_property_read_bool(np, "autorepeat"))
		__set_bit(EV_REP, poll_dev->input->evbit);

	platform_set_drvdata(pdev, poll_dev);

	/* Set all columns to low */
	regmap_update_bits(priv->syscon, SYSCON_OFFSET, SYSCON1_KBDSCAN_MASK,
			   SYSCON1_KBDSCAN(1));

	err = input_register_polled_device(poll_dev);
	if (err)
		goto out_err;

	return 0;

out_err:
	input_free_polled_device(poll_dev);
	return err;
}

static int clps711x_keypad_remove(struct platform_device *pdev)
{
	struct input_polled_dev *poll_dev = platform_get_drvdata(pdev);

	input_unregister_polled_device(poll_dev);
	input_free_polled_device(poll_dev);

	return 0;
}

static const struct of_device_id clps711x_keypad_of_match[] = {
	{ .compatible = "cirrus,clps711x-keypad", },
	{ }
};
MODULE_DEVICE_TABLE(of, clps711x_keypad_of_match);

static struct platform_driver clps711x_keypad_driver = {
	.driver	= {
		.name		= "clps711x-keypad",
		.of_match_table	= clps711x_keypad_of_match,
	},
	.probe	= clps711x_keypad_probe,
	.remove	= clps711x_keypad_remove,
};
module_platform_driver(clps711x_keypad_driver);

MODULE_AUTHOR("Alexander Shiyan <shc_work@mail.ru>");
MODULE_DESCRIPTION("Cirrus Logic CLPS711X Keypad driver");
MODULE_LICENSE("GPL");
