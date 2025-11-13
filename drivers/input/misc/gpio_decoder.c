// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2016 Texas Instruments Incorporated - http://www.ti.com/
 *
 * A generic driver to read multiple gpio lines and translate the
 * encoded numeric value into an input event.
 */

#include <linux/bitmap.h>
#include <linux/dev_printk.h>
#include <linux/device/devres.h>
#include <linux/err.h>
#include <linux/gpio/consumer.h>
#include <linux/input.h>
#include <linux/minmax.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/property.h>
#include <linux/types.h>

struct gpio_decoder {
	struct gpio_descs *input_gpios;
	struct device *dev;
	u32 axis;
	u32 last_stable;
};

static int gpio_decoder_get_gpios_state(struct gpio_decoder *decoder)
{
	struct gpio_descs *gpios = decoder->input_gpios;
	DECLARE_BITMAP(values, 32);
	unsigned int size;
	int err;

	size = min(gpios->ndescs, 32U);
	err = gpiod_get_array_value_cansleep(size, gpios->desc, gpios->info, values);
	if (err) {
		dev_err(decoder->dev, "Error reading GPIO: %d\n", err);
		return err;
	}

	return bitmap_read(values, 0, size);
}

static void gpio_decoder_poll_gpios(struct input_dev *input)
{
	struct gpio_decoder *decoder = input_get_drvdata(input);
	int state;

	state = gpio_decoder_get_gpios_state(decoder);
	if (state >= 0 && state != decoder->last_stable) {
		input_report_abs(input, decoder->axis, state);
		input_sync(input);
		decoder->last_stable = state;
	}
}

static int gpio_decoder_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct gpio_decoder *decoder;
	struct input_dev *input;
	u32 max;
	int err;

	decoder = devm_kzalloc(dev, sizeof(*decoder), GFP_KERNEL);
	if (!decoder)
		return -ENOMEM;

	decoder->dev = dev;
	device_property_read_u32(dev, "linux,axis", &decoder->axis);

	decoder->input_gpios = devm_gpiod_get_array(dev, NULL, GPIOD_IN);
	if (IS_ERR(decoder->input_gpios))
		return dev_err_probe(dev, PTR_ERR(decoder->input_gpios),
				     "unable to acquire input gpios\n");

	if (decoder->input_gpios->ndescs < 2)
		return dev_err_probe(dev, -EINVAL, "not enough gpios found\n");

	if (decoder->input_gpios->ndescs > 31)
		return dev_err_probe(dev, -EINVAL, "too many gpios found\n");

	if (device_property_read_u32(dev, "decoder-max-value", &max))
		max = BIT(decoder->input_gpios->ndescs) - 1;

	input = devm_input_allocate_device(dev);
	if (!input)
		return -ENOMEM;

	input_set_drvdata(input, decoder);

	input->name = pdev->name;
	input->id.bustype = BUS_HOST;
	input_set_abs_params(input, decoder->axis, 0, max, 0, 0);

	err = input_setup_polling(input, gpio_decoder_poll_gpios);
	if (err)
		return dev_err_probe(dev, err, "failed to set up polling\n");

	err = input_register_device(input);
	if (err)
		return dev_err_probe(dev, err, "failed to register input device\n");

	return 0;
}

static const struct of_device_id gpio_decoder_of_match[] = {
	{ .compatible = "gpio-decoder", },
	{ }
};
MODULE_DEVICE_TABLE(of, gpio_decoder_of_match);

static struct platform_driver gpio_decoder_driver = {
	.probe		= gpio_decoder_probe,
	.driver		= {
		.name	= "gpio-decoder",
		.of_match_table = gpio_decoder_of_match,
	}
};
module_platform_driver(gpio_decoder_driver);

MODULE_DESCRIPTION("GPIO decoder input driver");
MODULE_AUTHOR("Vignesh R <vigneshr@ti.com>");
MODULE_LICENSE("GPL v2");
