// SPDX-License-Identifier: GPL-2.0
/*
 * ADC generic resistive touchscreen (GRTS)
 * This is a generic input driver that connects to an ADC
 * given the channels in device tree, and reports events to the input
 * subsystem.
 *
 * Copyright (C) 2017,2018 Microchip Technology,
 * Author: Eugen Hristev <eugen.hristev@microchip.com>
 *
 */
#include <linux/input.h>
#include <linux/input/touchscreen.h>
#include <linux/iio/consumer.h>
#include <linux/iio/iio.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>

#define DRIVER_NAME					"resistive-adc-touch"
#define GRTS_DEFAULT_PRESSURE_MIN			50000
#define GRTS_MAX_POS_MASK				GENMASK(11, 0)

/**
 * grts_state - generic resistive touch screen information struct
 * @pressure_min:	number representing the minimum for the pressure
 * @pressure:		are we getting pressure info or not
 * @iio_chans:		list of channels acquired
 * @iio_cb:		iio_callback buffer for the data
 * @input:		the input device structure that we register
 * @prop:		touchscreen properties struct
 */
struct grts_state {
	u32				pressure_min;
	bool				pressure;
	struct iio_channel		*iio_chans;
	struct iio_cb_buffer		*iio_cb;
	struct input_dev		*input;
	struct touchscreen_properties	prop;
};

static int grts_cb(const void *data, void *private)
{
	const u16 *touch_info = data;
	struct grts_state *st = private;
	unsigned int x, y, press = 0x0;

	/* channel data coming in buffer in the order below */
	x = touch_info[0];
	y = touch_info[1];
	if (st->pressure)
		press = touch_info[2];

	if ((!x && !y) || (st->pressure && (press < st->pressure_min))) {
		/* report end of touch */
		input_report_key(st->input, BTN_TOUCH, 0);
		input_sync(st->input);
		return 0;
	}

	/* report proper touch to subsystem*/
	touchscreen_report_pos(st->input, &st->prop, x, y, false);
	if (st->pressure)
		input_report_abs(st->input, ABS_PRESSURE, press);
	input_report_key(st->input, BTN_TOUCH, 1);
	input_sync(st->input);

	return 0;
}

static int grts_open(struct input_dev *dev)
{
	int error;
	struct grts_state *st = input_get_drvdata(dev);

	error = iio_channel_start_all_cb(st->iio_cb);
	if (error) {
		dev_err(dev->dev.parent, "failed to start callback buffer.\n");
		return error;
	}
	return 0;
}

static void grts_close(struct input_dev *dev)
{
	struct grts_state *st = input_get_drvdata(dev);

	iio_channel_stop_all_cb(st->iio_cb);
}

static void grts_disable(void *data)
{
	iio_channel_release_all_cb(data);
}

static int grts_probe(struct platform_device *pdev)
{
	struct grts_state *st;
	struct input_dev *input;
	struct device *dev = &pdev->dev;
	struct iio_channel *chan;
	int error;

	st = devm_kzalloc(dev, sizeof(struct grts_state), GFP_KERNEL);
	if (!st)
		return -ENOMEM;

	/* get the channels from IIO device */
	st->iio_chans = devm_iio_channel_get_all(dev);
	if (IS_ERR(st->iio_chans)) {
		error = PTR_ERR(st->iio_chans);
		if (error != -EPROBE_DEFER)
			dev_err(dev, "can't get iio channels.\n");
		return error;
	}

	chan = &st->iio_chans[0];
	st->pressure = false;
	while (chan && chan->indio_dev) {
		if (!strcmp(chan->channel->datasheet_name, "pressure"))
			st->pressure = true;
		chan++;
	}

	if (st->pressure) {
		error = device_property_read_u32(dev,
						 "touchscreen-min-pressure",
						 &st->pressure_min);
		if (error) {
			dev_dbg(dev, "can't get touchscreen-min-pressure property.\n");
			st->pressure_min = GRTS_DEFAULT_PRESSURE_MIN;
		}
	}

	input = devm_input_allocate_device(dev);
	if (!input) {
		dev_err(dev, "failed to allocate input device.\n");
		return -ENOMEM;
	}

	input->name = DRIVER_NAME;
	input->id.bustype = BUS_HOST;
	input->open = grts_open;
	input->close = grts_close;

	input_set_abs_params(input, ABS_X, 0, GRTS_MAX_POS_MASK - 1, 0, 0);
	input_set_abs_params(input, ABS_Y, 0, GRTS_MAX_POS_MASK - 1, 0, 0);
	if (st->pressure)
		input_set_abs_params(input, ABS_PRESSURE, st->pressure_min,
				     0xffff, 0, 0);

	input_set_capability(input, EV_KEY, BTN_TOUCH);

	/* parse optional device tree properties */
	touchscreen_parse_properties(input, false, &st->prop);

	st->input = input;
	input_set_drvdata(input, st);

	error = input_register_device(input);
	if (error) {
		dev_err(dev, "failed to register input device.");
		return error;
	}

	st->iio_cb = iio_channel_get_all_cb(dev, grts_cb, st);
	if (IS_ERR(st->iio_cb)) {
		dev_err(dev, "failed to allocate callback buffer.\n");
		return PTR_ERR(st->iio_cb);
	}

	error = devm_add_action_or_reset(dev, grts_disable, st->iio_cb);
	if (error) {
		dev_err(dev, "failed to add disable action.\n");
		return error;
	}

	return 0;
}

static const struct of_device_id grts_of_match[] = {
	{
		.compatible = "resistive-adc-touch",
	}, {
		/* sentinel */
	},
};

MODULE_DEVICE_TABLE(of, grts_of_match);

static struct platform_driver grts_driver = {
	.probe = grts_probe,
	.driver = {
		.name = DRIVER_NAME,
		.of_match_table = of_match_ptr(grts_of_match),
	},
};

module_platform_driver(grts_driver);

MODULE_AUTHOR("Eugen Hristev <eugen.hristev@microchip.com>");
MODULE_DESCRIPTION("Generic ADC Resistive Touch Driver");
MODULE_LICENSE("GPL v2");
