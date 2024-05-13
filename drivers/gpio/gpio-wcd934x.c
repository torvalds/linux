// SPDX-License-Identifier: GPL-2.0
// Copyright (c) 2019, Linaro Limited

#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/gpio/driver.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/slab.h>

#define WCD_PIN_MASK(p) BIT(p)
#define WCD_REG_DIR_CTL_OFFSET 0x42
#define WCD_REG_VAL_CTL_OFFSET 0x43
#define WCD934X_NPINS		5

struct wcd_gpio_data {
	struct regmap *map;
	struct gpio_chip chip;
};

static int wcd_gpio_get_direction(struct gpio_chip *chip, unsigned int pin)
{
	struct wcd_gpio_data *data = gpiochip_get_data(chip);
	unsigned int value;
	int ret;

	ret = regmap_read(data->map, WCD_REG_DIR_CTL_OFFSET, &value);
	if (ret < 0)
		return ret;

	if (value & WCD_PIN_MASK(pin))
		return GPIO_LINE_DIRECTION_OUT;

	return GPIO_LINE_DIRECTION_IN;
}

static int wcd_gpio_direction_input(struct gpio_chip *chip, unsigned int pin)
{
	struct wcd_gpio_data *data = gpiochip_get_data(chip);

	return regmap_update_bits(data->map, WCD_REG_DIR_CTL_OFFSET,
				  WCD_PIN_MASK(pin), 0);
}

static int wcd_gpio_direction_output(struct gpio_chip *chip, unsigned int pin,
				     int val)
{
	struct wcd_gpio_data *data = gpiochip_get_data(chip);

	regmap_update_bits(data->map, WCD_REG_DIR_CTL_OFFSET,
			   WCD_PIN_MASK(pin), WCD_PIN_MASK(pin));

	return regmap_update_bits(data->map, WCD_REG_VAL_CTL_OFFSET,
				  WCD_PIN_MASK(pin),
				  val ? WCD_PIN_MASK(pin) : 0);
}

static int wcd_gpio_get(struct gpio_chip *chip, unsigned int pin)
{
	struct wcd_gpio_data *data = gpiochip_get_data(chip);
	unsigned int value;

	regmap_read(data->map, WCD_REG_VAL_CTL_OFFSET, &value);

	return !!(value & WCD_PIN_MASK(pin));
}

static void wcd_gpio_set(struct gpio_chip *chip, unsigned int pin, int val)
{
	struct wcd_gpio_data *data = gpiochip_get_data(chip);

	regmap_update_bits(data->map, WCD_REG_VAL_CTL_OFFSET,
			   WCD_PIN_MASK(pin), val ? WCD_PIN_MASK(pin) : 0);
}

static int wcd_gpio_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct wcd_gpio_data *data;
	struct gpio_chip *chip;

	data = devm_kzalloc(dev, sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	data->map = dev_get_regmap(dev->parent, NULL);
	if (!data->map) {
		dev_err(dev, "%s: failed to get regmap\n", __func__);
		return  -EINVAL;
	}

	chip = &data->chip;
	chip->direction_input  = wcd_gpio_direction_input;
	chip->direction_output = wcd_gpio_direction_output;
	chip->get_direction = wcd_gpio_get_direction;
	chip->get = wcd_gpio_get;
	chip->set = wcd_gpio_set;
	chip->parent = dev;
	chip->base = -1;
	chip->ngpio = WCD934X_NPINS;
	chip->label = dev_name(dev);
	chip->can_sleep = false;

	return devm_gpiochip_add_data(dev, chip, data);
}

static const struct of_device_id wcd_gpio_of_match[] = {
	{ .compatible = "qcom,wcd9340-gpio" },
	{ .compatible = "qcom,wcd9341-gpio" },
	{ }
};
MODULE_DEVICE_TABLE(of, wcd_gpio_of_match);

static struct platform_driver wcd_gpio_driver = {
	.driver = {
		   .name = "wcd934x-gpio",
		   .of_match_table = wcd_gpio_of_match,
	},
	.probe = wcd_gpio_probe,
};

module_platform_driver(wcd_gpio_driver);
MODULE_DESCRIPTION("Qualcomm Technologies, Inc WCD GPIO control driver");
MODULE_LICENSE("GPL v2");
