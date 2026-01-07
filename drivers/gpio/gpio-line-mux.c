// SPDX-License-Identifier: GPL-2.0
/*
 * GPIO line mux which acts as virtual gpiochip and provides a 1-to-many
 * mapping between virtual GPIOs and a real GPIO + multiplexer.
 *
 * Copyright (c) 2025 Jonas Jelonek <jelonek.jonas@gmail.com>
 */

#include <linux/gpio/consumer.h>
#include <linux/gpio/driver.h>
#include <linux/mod_devicetable.h>
#include <linux/mutex.h>
#include <linux/mux/consumer.h>
#include <linux/platform_device.h>

#define MUX_SELECT_DELAY_US	100

struct gpio_lmux {
	struct gpio_chip gc;
	struct mux_control *mux;
	struct gpio_desc *muxed_gpio;

	u32 num_gpio_mux_states;
	unsigned int gpio_mux_states[] __counted_by(num_gpio_mux_states);
};

static int gpio_lmux_gpio_get(struct gpio_chip *gc, unsigned int offset)
{
	struct gpio_lmux *glm = gpiochip_get_data(gc);
	int ret;

	ret = mux_control_select_delay(glm->mux, glm->gpio_mux_states[offset],
				       MUX_SELECT_DELAY_US);
	if (ret < 0)
		return ret;

	ret = gpiod_get_raw_value_cansleep(glm->muxed_gpio);
	mux_control_deselect(glm->mux);
	return ret;
}

static int gpio_lmux_gpio_get_direction(struct gpio_chip *gc,
					unsigned int offset)
{
	return GPIO_LINE_DIRECTION_IN;
}

static int gpio_lmux_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct gpio_lmux *glm;
	unsigned int ngpio;
	size_t size;
	int ret;

	ngpio = device_property_count_u32(dev, "gpio-line-mux-states");
	if (!ngpio)
		return -EINVAL;

	size = struct_size(glm, gpio_mux_states, ngpio);
	glm = devm_kzalloc(dev, size, GFP_KERNEL);
	if (!glm)
		return -ENOMEM;

	glm->gc.base = -1;
	glm->gc.can_sleep = true;
	glm->gc.fwnode = dev_fwnode(dev);
	glm->gc.label = dev_name(dev);
	glm->gc.ngpio = ngpio;
	glm->gc.owner = THIS_MODULE;
	glm->gc.parent = dev;

	glm->gc.get = gpio_lmux_gpio_get;
	glm->gc.get_direction = gpio_lmux_gpio_get_direction;

	glm->mux = devm_mux_control_get(dev, NULL);
	if (IS_ERR(glm->mux))
		return dev_err_probe(dev, PTR_ERR(glm->mux),
				     "could not get mux controller\n");

	glm->muxed_gpio = devm_gpiod_get(dev, "muxed", GPIOD_IN);
	if (IS_ERR(glm->muxed_gpio))
		return dev_err_probe(dev, PTR_ERR(glm->muxed_gpio),
				     "could not get muxed-gpio\n");

	glm->num_gpio_mux_states = ngpio;
	ret = device_property_read_u32_array(dev, "gpio-line-mux-states",
					     &glm->gpio_mux_states[0], ngpio);
	if (ret)
		return dev_err_probe(dev, ret, "could not get mux states\n");

	ret = devm_gpiochip_add_data(dev, &glm->gc, glm);
	if (ret)
		return dev_err_probe(dev, ret, "failed to add gpiochip\n");

	return 0;
}

static const struct of_device_id gpio_lmux_of_match[] = {
	{ .compatible = "gpio-line-mux" },
	{ }
};
MODULE_DEVICE_TABLE(of, gpio_lmux_of_match);

static struct platform_driver gpio_lmux_driver = {
	.driver = {
		.name = "gpio-line-mux",
		.of_match_table = gpio_lmux_of_match,
	},
	.probe = gpio_lmux_probe,
};
module_platform_driver(gpio_lmux_driver);

MODULE_AUTHOR("Jonas Jelonek <jelonek.jonas@gmail.com>");
MODULE_DESCRIPTION("GPIO line mux driver");
MODULE_LICENSE("GPL");
