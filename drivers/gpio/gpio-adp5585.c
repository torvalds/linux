// SPDX-License-Identifier: GPL-2.0-only
/*
 * Analog Devices ADP5585 GPIO driver
 *
 * Copyright 2022 NXP
 * Copyright 2024 Ideas on Board Oy
 */

#include <linux/device.h>
#include <linux/gpio/driver.h>
#include <linux/mfd/adp5585.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/types.h>

#define ADP5585_GPIO_MAX	11

struct adp5585_gpio_dev {
	struct gpio_chip gpio_chip;
	struct regmap *regmap;
};

static int adp5585_gpio_get_direction(struct gpio_chip *chip, unsigned int off)
{
	struct adp5585_gpio_dev *adp5585_gpio = gpiochip_get_data(chip);
	unsigned int bank = ADP5585_BANK(off);
	unsigned int bit = ADP5585_BIT(off);
	unsigned int val;

	regmap_read(adp5585_gpio->regmap, ADP5585_GPIO_DIRECTION_A + bank, &val);

	return val & bit ? GPIO_LINE_DIRECTION_OUT : GPIO_LINE_DIRECTION_IN;
}

static int adp5585_gpio_direction_input(struct gpio_chip *chip, unsigned int off)
{
	struct adp5585_gpio_dev *adp5585_gpio = gpiochip_get_data(chip);
	unsigned int bank = ADP5585_BANK(off);
	unsigned int bit = ADP5585_BIT(off);

	return regmap_clear_bits(adp5585_gpio->regmap,
				 ADP5585_GPIO_DIRECTION_A + bank, bit);
}

static int adp5585_gpio_direction_output(struct gpio_chip *chip, unsigned int off, int val)
{
	struct adp5585_gpio_dev *adp5585_gpio = gpiochip_get_data(chip);
	unsigned int bank = ADP5585_BANK(off);
	unsigned int bit = ADP5585_BIT(off);
	int ret;

	ret = regmap_update_bits(adp5585_gpio->regmap,
				 ADP5585_GPO_DATA_OUT_A + bank, bit,
				 val ? bit : 0);
	if (ret)
		return ret;

	return regmap_set_bits(adp5585_gpio->regmap,
			       ADP5585_GPIO_DIRECTION_A + bank, bit);
}

static int adp5585_gpio_get_value(struct gpio_chip *chip, unsigned int off)
{
	struct adp5585_gpio_dev *adp5585_gpio = gpiochip_get_data(chip);
	unsigned int bank = ADP5585_BANK(off);
	unsigned int bit = ADP5585_BIT(off);
	unsigned int reg;
	unsigned int val;

	/*
	 * The input status register doesn't reflect the pin state when the
	 * GPIO is configured as an output. Check the direction, and read the
	 * input status from GPI_STATUS or output value from GPO_DATA_OUT
	 * accordingly.
	 *
	 * We don't need any locking, as concurrent access to the same GPIO
	 * isn't allowed by the GPIO API, so there's no risk of the
	 * .direction_input(), .direction_output() or .set() operations racing
	 * with this.
	 */
	regmap_read(adp5585_gpio->regmap, ADP5585_GPIO_DIRECTION_A + bank, &val);
	reg = val & bit ? ADP5585_GPO_DATA_OUT_A : ADP5585_GPI_STATUS_A;
	regmap_read(adp5585_gpio->regmap, reg + bank, &val);

	return !!(val & bit);
}

static int adp5585_gpio_set_value(struct gpio_chip *chip, unsigned int off,
				  int val)
{
	struct adp5585_gpio_dev *adp5585_gpio = gpiochip_get_data(chip);
	unsigned int bank = ADP5585_BANK(off);
	unsigned int bit = ADP5585_BIT(off);

	return regmap_update_bits(adp5585_gpio->regmap,
				  ADP5585_GPO_DATA_OUT_A + bank,
				  bit, val ? bit : 0);
}

static int adp5585_gpio_set_bias(struct adp5585_gpio_dev *adp5585_gpio,
				 unsigned int off, unsigned int bias)
{
	unsigned int bit, reg, mask, val;

	/*
	 * The bias configuration fields are 2 bits wide and laid down in
	 * consecutive registers ADP5585_RPULL_CONFIG_*, with a hole of 4 bits
	 * after R5.
	 */
	bit = off * 2 + (off > 5 ? 4 : 0);
	reg = ADP5585_RPULL_CONFIG_A + bit / 8;
	mask = ADP5585_Rx_PULL_CFG_MASK << (bit % 8);
	val = bias << (bit % 8);

	return regmap_update_bits(adp5585_gpio->regmap, reg, mask, val);
}

static int adp5585_gpio_set_drive(struct adp5585_gpio_dev *adp5585_gpio,
				  unsigned int off, enum pin_config_param drive)
{
	unsigned int bank = ADP5585_BANK(off);
	unsigned int bit = ADP5585_BIT(off);

	return regmap_update_bits(adp5585_gpio->regmap,
				  ADP5585_GPO_OUT_MODE_A + bank, bit,
				  drive == PIN_CONFIG_DRIVE_OPEN_DRAIN ? bit : 0);
}

static int adp5585_gpio_set_debounce(struct adp5585_gpio_dev *adp5585_gpio,
				     unsigned int off, unsigned int debounce)
{
	unsigned int bank = ADP5585_BANK(off);
	unsigned int bit = ADP5585_BIT(off);

	return regmap_update_bits(adp5585_gpio->regmap,
				  ADP5585_DEBOUNCE_DIS_A + bank, bit,
				  debounce ? 0 : bit);
}

static int adp5585_gpio_set_config(struct gpio_chip *chip, unsigned int off,
				   unsigned long config)
{
	struct adp5585_gpio_dev *adp5585_gpio = gpiochip_get_data(chip);
	enum pin_config_param param = pinconf_to_config_param(config);
	u32 arg = pinconf_to_config_argument(config);

	switch (param) {
	case PIN_CONFIG_BIAS_DISABLE:
		return adp5585_gpio_set_bias(adp5585_gpio, off,
					     ADP5585_Rx_PULL_CFG_DISABLE);

	case PIN_CONFIG_BIAS_PULL_DOWN:
		return adp5585_gpio_set_bias(adp5585_gpio, off, arg ?
					     ADP5585_Rx_PULL_CFG_PD_300K :
					     ADP5585_Rx_PULL_CFG_DISABLE);

	case PIN_CONFIG_BIAS_PULL_UP:
		return adp5585_gpio_set_bias(adp5585_gpio, off, arg ?
					     ADP5585_Rx_PULL_CFG_PU_300K :
					     ADP5585_Rx_PULL_CFG_DISABLE);

	case PIN_CONFIG_DRIVE_OPEN_DRAIN:
	case PIN_CONFIG_DRIVE_PUSH_PULL:
		return adp5585_gpio_set_drive(adp5585_gpio, off, param);

	case PIN_CONFIG_INPUT_DEBOUNCE:
		return adp5585_gpio_set_debounce(adp5585_gpio, off, arg);

	default:
		return -ENOTSUPP;
	};
}

static int adp5585_gpio_probe(struct platform_device *pdev)
{
	struct adp5585_dev *adp5585 = dev_get_drvdata(pdev->dev.parent);
	struct adp5585_gpio_dev *adp5585_gpio;
	struct device *dev = &pdev->dev;
	struct gpio_chip *gc;
	int ret;

	adp5585_gpio = devm_kzalloc(dev, sizeof(*adp5585_gpio), GFP_KERNEL);
	if (!adp5585_gpio)
		return -ENOMEM;

	adp5585_gpio->regmap = adp5585->regmap;

	device_set_of_node_from_dev(dev, dev->parent);

	gc = &adp5585_gpio->gpio_chip;
	gc->parent = dev;
	gc->get_direction = adp5585_gpio_get_direction;
	gc->direction_input = adp5585_gpio_direction_input;
	gc->direction_output = adp5585_gpio_direction_output;
	gc->get = adp5585_gpio_get_value;
	gc->set_rv = adp5585_gpio_set_value;
	gc->set_config = adp5585_gpio_set_config;
	gc->can_sleep = true;

	gc->base = -1;
	gc->ngpio = ADP5585_GPIO_MAX;
	gc->label = pdev->name;
	gc->owner = THIS_MODULE;

	ret = devm_gpiochip_add_data(dev, &adp5585_gpio->gpio_chip,
				     adp5585_gpio);
	if (ret)
		return dev_err_probe(dev, ret, "failed to add GPIO chip\n");

	return 0;
}

static const struct platform_device_id adp5585_gpio_id_table[] = {
	{ "adp5585-gpio" },
	{ /* Sentinel */ }
};
MODULE_DEVICE_TABLE(platform, adp5585_gpio_id_table);

static struct platform_driver adp5585_gpio_driver = {
	.driver	= {
		.name = "adp5585-gpio",
	},
	.probe = adp5585_gpio_probe,
	.id_table = adp5585_gpio_id_table,
};
module_platform_driver(adp5585_gpio_driver);

MODULE_AUTHOR("Haibo Chen <haibo.chen@nxp.com>");
MODULE_DESCRIPTION("GPIO ADP5585 Driver");
MODULE_LICENSE("GPL");
