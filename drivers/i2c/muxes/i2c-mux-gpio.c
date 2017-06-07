/*
 * I2C multiplexer using GPIO API
 *
 * Peter Korsgaard <peter.korsgaard@barco.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/i2c.h>
#include <linux/i2c-mux.h>
#include <linux/i2c-mux-gpio.h>
#include <linux/platform_device.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/gpio.h>
#include "../../gpio/gpiolib.h"
#include <linux/of_gpio.h>

struct gpiomux {
	struct i2c_mux_gpio_platform_data data;
	unsigned gpio_base;
	struct gpio_desc **gpios;
	int *values;
};

static void i2c_mux_gpio_set(const struct gpiomux *mux, unsigned val)
{
	int i;

	for (i = 0; i < mux->data.n_gpios; i++)
		mux->values[i] = (val >> i) & 1;

	gpiod_set_array_value_cansleep(mux->data.n_gpios,
				       mux->gpios, mux->values);
}

static int i2c_mux_gpio_select(struct i2c_mux_core *muxc, u32 chan)
{
	struct gpiomux *mux = i2c_mux_priv(muxc);

	i2c_mux_gpio_set(mux, chan);

	return 0;
}

static int i2c_mux_gpio_deselect(struct i2c_mux_core *muxc, u32 chan)
{
	struct gpiomux *mux = i2c_mux_priv(muxc);

	i2c_mux_gpio_set(mux, mux->data.idle);

	return 0;
}

static int match_gpio_chip_by_label(struct gpio_chip *chip,
					      void *data)
{
	return !strcmp(chip->label, data);
}

#ifdef CONFIG_OF
static int i2c_mux_gpio_probe_dt(struct gpiomux *mux,
					struct platform_device *pdev)
{
	struct device_node *np = pdev->dev.of_node;
	struct device_node *adapter_np, *child;
	struct i2c_adapter *adapter;
	unsigned *values, *gpios;
	int i = 0, ret;

	if (!np)
		return -ENODEV;

	adapter_np = of_parse_phandle(np, "i2c-parent", 0);
	if (!adapter_np) {
		dev_err(&pdev->dev, "Cannot parse i2c-parent\n");
		return -ENODEV;
	}
	adapter = of_find_i2c_adapter_by_node(adapter_np);
	of_node_put(adapter_np);
	if (!adapter)
		return -EPROBE_DEFER;

	mux->data.parent = i2c_adapter_id(adapter);
	put_device(&adapter->dev);

	mux->data.n_values = of_get_child_count(np);

	values = devm_kzalloc(&pdev->dev,
			      sizeof(*mux->data.values) * mux->data.n_values,
			      GFP_KERNEL);
	if (!values) {
		dev_err(&pdev->dev, "Cannot allocate values array");
		return -ENOMEM;
	}

	for_each_child_of_node(np, child) {
		of_property_read_u32(child, "reg", values + i);
		i++;
	}
	mux->data.values = values;

	if (of_property_read_u32(np, "idle-state", &mux->data.idle))
		mux->data.idle = I2C_MUX_GPIO_NO_IDLE;

	mux->data.n_gpios = of_gpio_named_count(np, "mux-gpios");
	if (mux->data.n_gpios < 0) {
		dev_err(&pdev->dev, "Missing mux-gpios property in the DT.\n");
		return -EINVAL;
	}

	gpios = devm_kzalloc(&pdev->dev,
			     sizeof(*mux->data.gpios) * mux->data.n_gpios, GFP_KERNEL);
	if (!gpios) {
		dev_err(&pdev->dev, "Cannot allocate gpios array");
		return -ENOMEM;
	}

	for (i = 0; i < mux->data.n_gpios; i++) {
		ret = of_get_named_gpio(np, "mux-gpios", i);
		if (ret < 0)
			return ret;
		gpios[i] = ret;
	}

	mux->data.gpios = gpios;

	return 0;
}
#else
static int i2c_mux_gpio_probe_dt(struct gpiomux *mux,
					struct platform_device *pdev)
{
	return 0;
}
#endif

static int i2c_mux_gpio_probe(struct platform_device *pdev)
{
	struct i2c_mux_core *muxc;
	struct gpiomux *mux;
	struct i2c_adapter *parent;
	struct i2c_adapter *root;
	unsigned initial_state, gpio_base;
	int i, ret;

	mux = devm_kzalloc(&pdev->dev, sizeof(*mux), GFP_KERNEL);
	if (!mux)
		return -ENOMEM;

	if (!dev_get_platdata(&pdev->dev)) {
		ret = i2c_mux_gpio_probe_dt(mux, pdev);
		if (ret < 0)
			return ret;
	} else {
		memcpy(&mux->data, dev_get_platdata(&pdev->dev),
			sizeof(mux->data));
	}

	/*
	 * If a GPIO chip name is provided, the GPIO pin numbers provided are
	 * relative to its base GPIO number. Otherwise they are absolute.
	 */
	if (mux->data.gpio_chip) {
		struct gpio_chip *gpio;

		gpio = gpiochip_find(mux->data.gpio_chip,
				     match_gpio_chip_by_label);
		if (!gpio)
			return -EPROBE_DEFER;

		gpio_base = gpio->base;
	} else {
		gpio_base = 0;
	}

	parent = i2c_get_adapter(mux->data.parent);
	if (!parent)
		return -EPROBE_DEFER;

	muxc = i2c_mux_alloc(parent, &pdev->dev, mux->data.n_values,
			     mux->data.n_gpios * sizeof(*mux->gpios) +
			     mux->data.n_gpios * sizeof(*mux->values), 0,
			     i2c_mux_gpio_select, NULL);
	if (!muxc) {
		ret = -ENOMEM;
		goto alloc_failed;
	}
	mux->gpios = muxc->priv;
	mux->values = (int *)(mux->gpios + mux->data.n_gpios);
	muxc->priv = mux;

	platform_set_drvdata(pdev, muxc);

	root = i2c_root_adapter(&parent->dev);

	muxc->mux_locked = true;
	mux->gpio_base = gpio_base;

	if (mux->data.idle != I2C_MUX_GPIO_NO_IDLE) {
		initial_state = mux->data.idle;
		muxc->deselect = i2c_mux_gpio_deselect;
	} else {
		initial_state = mux->data.values[0];
	}

	for (i = 0; i < mux->data.n_gpios; i++) {
		struct device *gpio_dev;
		struct gpio_desc *gpio_desc;

		ret = gpio_request(gpio_base + mux->data.gpios[i], "i2c-mux-gpio");
		if (ret) {
			dev_err(&pdev->dev, "Failed to request GPIO %d\n",
				mux->data.gpios[i]);
			goto err_request_gpio;
		}

		ret = gpio_direction_output(gpio_base + mux->data.gpios[i],
					    initial_state & (1 << i));
		if (ret) {
			dev_err(&pdev->dev,
				"Failed to set direction of GPIO %d to output\n",
				mux->data.gpios[i]);
			i++;	/* gpio_request above succeeded, so must free */
			goto err_request_gpio;
		}

		gpio_desc = gpio_to_desc(gpio_base + mux->data.gpios[i]);
		mux->gpios[i] = gpio_desc;

		if (!muxc->mux_locked)
			continue;

		gpio_dev = &gpio_desc->gdev->dev;
		muxc->mux_locked = i2c_root_adapter(gpio_dev) == root;
	}

	if (muxc->mux_locked)
		dev_info(&pdev->dev, "mux-locked i2c mux\n");

	for (i = 0; i < mux->data.n_values; i++) {
		u32 nr = mux->data.base_nr ? (mux->data.base_nr + i) : 0;
		unsigned int class = mux->data.classes ? mux->data.classes[i] : 0;

		ret = i2c_mux_add_adapter(muxc, nr, mux->data.values[i], class);
		if (ret)
			goto add_adapter_failed;
	}

	dev_info(&pdev->dev, "%d port mux on %s adapter\n",
		 mux->data.n_values, parent->name);

	return 0;

add_adapter_failed:
	i2c_mux_del_adapters(muxc);
	i = mux->data.n_gpios;
err_request_gpio:
	for (; i > 0; i--)
		gpio_free(gpio_base + mux->data.gpios[i - 1]);
alloc_failed:
	i2c_put_adapter(parent);

	return ret;
}

static int i2c_mux_gpio_remove(struct platform_device *pdev)
{
	struct i2c_mux_core *muxc = platform_get_drvdata(pdev);
	struct gpiomux *mux = i2c_mux_priv(muxc);
	int i;

	i2c_mux_del_adapters(muxc);

	for (i = 0; i < mux->data.n_gpios; i++)
		gpio_free(mux->gpio_base + mux->data.gpios[i]);

	i2c_put_adapter(muxc->parent);

	return 0;
}

static const struct of_device_id i2c_mux_gpio_of_match[] = {
	{ .compatible = "i2c-mux-gpio", },
	{},
};
MODULE_DEVICE_TABLE(of, i2c_mux_gpio_of_match);

static struct platform_driver i2c_mux_gpio_driver = {
	.probe	= i2c_mux_gpio_probe,
	.remove	= i2c_mux_gpio_remove,
	.driver	= {
		.name	= "i2c-mux-gpio",
		.of_match_table = i2c_mux_gpio_of_match,
	},
};

module_platform_driver(i2c_mux_gpio_driver);

MODULE_DESCRIPTION("GPIO-based I2C multiplexer driver");
MODULE_AUTHOR("Peter Korsgaard <peter.korsgaard@barco.com>");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:i2c-mux-gpio");
