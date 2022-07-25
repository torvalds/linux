// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Cirrus Logic CLPS711X Keypad driver
 *
 * Copyright (C) 2014 Alexander Shiyan <shc_work@mail.ru>
 */

#include <linux/input.h>
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

static void clps711x_keypad_poll(struct input_dev *input)
{
	const unsigned short *keycodes = input->keycode;
	struct clps711x_keypad_data *priv = input_get_drvdata(input);
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
					input_event(input,
						    EV_MSC, MSC_SCAN, code);
				} else {
					clear_bit(col, data->last_state);
				}

				if (keycodes[code])
					input_report_key(input,
							 keycodes[code], state);
				sync = true;
			}
		}

		/* Set all columns to low */
		regmap_update_bits(priv->syscon, SYSCON_OFFSET,
				   SYSCON1_KBDSCAN_MASK, SYSCON1_KBDSCAN(1));
	}

	if (sync)
		input_sync(input);
}

static int clps711x_keypad_probe(struct platform_device *pdev)
{
	struct clps711x_keypad_data *priv;
	struct device *dev = &pdev->dev;
	struct device_node *np = dev->of_node;
	struct input_dev *input;
	u32 poll_interval;
	int i, err;

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->syscon = syscon_regmap_lookup_by_phandle(np, "syscon");
	if (IS_ERR(priv->syscon))
		return PTR_ERR(priv->syscon);

	priv->row_count = of_gpio_named_count(np, "row-gpios");
	if (priv->row_count < 1)
		return -EINVAL;

	priv->gpio_data = devm_kcalloc(dev,
				priv->row_count, sizeof(*priv->gpio_data),
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

	input = devm_input_allocate_device(dev);
	if (!input)
		return -ENOMEM;

	input_set_drvdata(input, priv);

	input->name		= pdev->name;
	input->dev.parent	= dev;
	input->id.bustype	= BUS_HOST;
	input->id.vendor	= 0x0001;
	input->id.product	= 0x0001;
	input->id.version	= 0x0100;

	err = matrix_keypad_build_keymap(NULL, NULL, priv->row_count,
					 CLPS711X_KEYPAD_COL_COUNT,
					 NULL, input);
	if (err)
		return err;

	input_set_capability(input, EV_MSC, MSC_SCAN);
	if (of_property_read_bool(np, "autorepeat"))
		__set_bit(EV_REP, input->evbit);

	/* Set all columns to low */
	regmap_update_bits(priv->syscon, SYSCON_OFFSET, SYSCON1_KBDSCAN_MASK,
			   SYSCON1_KBDSCAN(1));


	err = input_setup_polling(input, clps711x_keypad_poll);
	if (err)
		return err;

	input_set_poll_interval(input, poll_interval);

	err = input_register_device(input);
	if (err)
		return err;

	return 0;
}

static const struct of_device_id clps711x_keypad_of_match[] = {
	{ .compatible = "cirrus,ep7209-keypad", },
	{ }
};
MODULE_DEVICE_TABLE(of, clps711x_keypad_of_match);

static struct platform_driver clps711x_keypad_driver = {
	.driver	= {
		.name		= "clps711x-keypad",
		.of_match_table	= clps711x_keypad_of_match,
	},
	.probe	= clps711x_keypad_probe,
};
module_platform_driver(clps711x_keypad_driver);

MODULE_AUTHOR("Alexander Shiyan <shc_work@mail.ru>");
MODULE_DESCRIPTION("Cirrus Logic CLPS711X Keypad driver");
MODULE_LICENSE("GPL");
