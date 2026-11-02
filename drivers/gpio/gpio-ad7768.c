// SPDX-License-Identifier: GPL-2.0
/*
 * Analog Devices AD7768 GPIO auxiliary driver
 *
 * Copyright 2026 Analog Devices Inc.
 */

#include <linux/auxiliary_bus.h>
#include <linux/bitops.h>
#include <linux/device.h>
#include <linux/err.h>
#include <linux/gpio/driver.h>
#include <linux/gpio/regmap.h>
#include <linux/module.h>
#include <linux/pm_runtime.h>
#include <linux/property.h>
#include <linux/regmap.h>

#define AD7768_REG_GPIO_CONTROL		0x0E
#define   AD7768_GPIO_UGPIO_ENABLE	BIT(7)

#define AD7768_REG_GPIO_WRITE		0x0F
#define AD7768_REG_GPIO_READ		0x10

#define AD7768_NUM_GPIOS		5
#define AD7768_FILTER_GPIO		4

static int ad7768_gpio_init_valid_mask(struct gpio_chip *gc,
				       unsigned long *valid_mask,
				       unsigned int ngpios)
{
	if (ngpios > AD7768_FILTER_GPIO &&
	    fwnode_property_match_string(gc->fwnode, "clock-names", "mclk") < 0)
		__clear_bit(AD7768_FILTER_GPIO, valid_mask);

	return 0;
}

static int ad7768_gpio_reg_mask_xlate(struct gpio_regmap *gpio,
				      enum gpio_regmap_operation op,
				      unsigned int base, unsigned int offset,
				      unsigned int *reg, unsigned int *mask)
{
	struct regmap *regmap = gpio_regmap_get_drvdata(gpio);
	int ret;

	*reg = base;
	*mask = BIT(offset);

	if (op != GPIO_REGMAP_GET_OP)
		return 0;

	/*
	 * AD7768 has separate input-state and output-latch registers. For an
	 * output line, report the programmed value from the output latch;
	 * input lines continue to use the input-state register.
	 */
	ret = regmap_test_bits(regmap, AD7768_REG_GPIO_CONTROL, *mask);
	if (ret < 0)
		return ret;
	if (ret)
		*reg = AD7768_REG_GPIO_WRITE;

	return 0;
}

static int ad7768_gpio_probe(struct auxiliary_device *adev,
			     const struct auxiliary_device_id *id)
{
	struct gpio_regmap_config config;
	struct device *dev = &adev->dev;
	struct device *parent = dev->parent;
	struct regmap *map;
	int ret;

	map = dev_get_regmap(parent, NULL);
	if (!map)
		return -ENODEV;

	PM_RUNTIME_ACQUIRE_AUTOSUSPEND(parent, pm);
	ret = PM_RUNTIME_ACQUIRE_ERR(&pm);
	if (ret)
		return ret;

	ret = regmap_set_bits(map, AD7768_REG_GPIO_CONTROL, AD7768_GPIO_UGPIO_ENABLE);
	if (ret)
		return ret;

	config = (struct gpio_regmap_config) {
		.parent = dev,
		.regmap = map,
		.label = dev_name(parent),
		.ngpio = AD7768_NUM_GPIOS,
		.reg_dat_base = AD7768_REG_GPIO_READ,
		.reg_set_base = AD7768_REG_GPIO_WRITE,
		.reg_dir_out_base = AD7768_REG_GPIO_CONTROL,
		.pm_dev = parent,
		.fwnode = dev_fwnode(parent),
		.reg_mask_xlate = ad7768_gpio_reg_mask_xlate,
		.init_valid_mask = ad7768_gpio_init_valid_mask,
		.drvdata = map,
	};

	return PTR_ERR_OR_ZERO(devm_gpio_regmap_register(dev, &config));
}

static const struct auxiliary_device_id ad7768_gpio_ids[] = {
	{ .name = "ad7768.gpio" },
	{ }
};
MODULE_DEVICE_TABLE(auxiliary, ad7768_gpio_ids);

static struct auxiliary_driver ad7768_gpio_driver = {
	.probe = ad7768_gpio_probe,
	.id_table = ad7768_gpio_ids,
};
module_auxiliary_driver(ad7768_gpio_driver);

MODULE_AUTHOR("Janani Sunil <janani.sunil@analog.com>");
MODULE_DESCRIPTION("Analog Devices AD7768 GPIO auxiliary driver");
MODULE_LICENSE("GPL");
