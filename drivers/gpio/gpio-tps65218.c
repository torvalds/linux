// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright 2015 Verifone Int.
 *
 * Author: Nicolas Saenz Julienne <nicolassaenzj@gmail.com>
 *
 * This driver is based on the gpio-tps65912 implementation.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/errno.h>
#include <linux/gpio/driver.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/mfd/tps65218.h>

struct tps65218_gpio {
	struct tps65218 *tps65218;
	struct gpio_chip gpio_chip;
};

static int tps65218_gpio_get(struct gpio_chip *gc, unsigned offset)
{
	struct tps65218_gpio *tps65218_gpio = gpiochip_get_data(gc);
	struct tps65218 *tps65218 = tps65218_gpio->tps65218;
	unsigned int val;
	int ret;

	ret = regmap_read(tps65218->regmap, TPS65218_REG_ENABLE2, &val);
	if (ret)
		return ret;

	return !!(val & (TPS65218_ENABLE2_GPIO1 << offset));
}

static void tps65218_gpio_set(struct gpio_chip *gc, unsigned offset,
			      int value)
{
	struct tps65218_gpio *tps65218_gpio = gpiochip_get_data(gc);
	struct tps65218 *tps65218 = tps65218_gpio->tps65218;

	if (value)
		tps65218_set_bits(tps65218, TPS65218_REG_ENABLE2,
				  TPS65218_ENABLE2_GPIO1 << offset,
				  TPS65218_ENABLE2_GPIO1 << offset,
				  TPS65218_PROTECT_L1);
	else
		tps65218_clear_bits(tps65218, TPS65218_REG_ENABLE2,
				    TPS65218_ENABLE2_GPIO1 << offset,
				    TPS65218_PROTECT_L1);
}

static int tps65218_gpio_output(struct gpio_chip *gc, unsigned offset,
				int value)
{
	/* Only drives GPOs */
	tps65218_gpio_set(gc, offset, value);
	return 0;
}

static int tps65218_gpio_input(struct gpio_chip *gc, unsigned offset)
{
	return -EPERM;
}

static int tps65218_gpio_request(struct gpio_chip *gc, unsigned offset)
{
	struct tps65218_gpio *tps65218_gpio = gpiochip_get_data(gc);
	struct tps65218 *tps65218 = tps65218_gpio->tps65218;
	int ret;

	if (gpiochip_line_is_open_source(gc, offset)) {
		dev_err(gc->parent, "can't work as open source\n");
		return -EINVAL;
	}

	switch (offset) {
	case 0:
		if (!gpiochip_line_is_open_drain(gc, offset)) {
			dev_err(gc->parent, "GPO1 works only as open drain\n");
			return -EINVAL;
		}

		/* Disable sequencer for GPO1 */
		ret = tps65218_clear_bits(tps65218, TPS65218_REG_SEQ7,
					  TPS65218_SEQ7_GPO1_SEQ_MASK,
					  TPS65218_PROTECT_L1);
		if (ret)
			return ret;

		/* Setup GPO1 */
		ret = tps65218_clear_bits(tps65218, TPS65218_REG_CONFIG1,
					  TPS65218_CONFIG1_IO1_SEL,
					  TPS65218_PROTECT_L1);
		if (ret)
			return ret;

		break;
	case 1:
		/* Setup GPO2 */
		ret = tps65218_clear_bits(tps65218, TPS65218_REG_CONFIG1,
					  TPS65218_CONFIG1_IO1_SEL,
					  TPS65218_PROTECT_L1);
		if (ret)
			return ret;

		break;

	case 2:
		if (!gpiochip_line_is_open_drain(gc, offset)) {
			dev_err(gc->parent, "GPO3 works only as open drain\n");
			return -EINVAL;
		}

		/* Disable sequencer for GPO3 */
		ret = tps65218_clear_bits(tps65218, TPS65218_REG_SEQ7,
					  TPS65218_SEQ7_GPO3_SEQ_MASK,
					  TPS65218_PROTECT_L1);
		if (ret)
			return ret;

		/* Setup GPO3 */
		ret = tps65218_clear_bits(tps65218, TPS65218_REG_CONFIG2,
					  TPS65218_CONFIG2_DC12_RST,
					  TPS65218_PROTECT_L1);
		if (ret)
			return ret;

		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int tps65218_gpio_set_config(struct gpio_chip *gc, unsigned offset,
				    unsigned long config)
{
	struct tps65218_gpio *tps65218_gpio = gpiochip_get_data(gc);
	struct tps65218 *tps65218 = tps65218_gpio->tps65218;
	enum pin_config_param param = pinconf_to_config_param(config);

	switch (offset) {
	case 0:
	case 2:
		/* GPO1 is hardwired to be open drain */
		if (param == PIN_CONFIG_DRIVE_OPEN_DRAIN)
			return 0;
		return -ENOTSUPP;
	case 1:
		/* GPO2 is push-pull by default, can be set as open drain. */
		if (param == PIN_CONFIG_DRIVE_OPEN_DRAIN)
			return tps65218_clear_bits(tps65218,
						   TPS65218_REG_CONFIG1,
						   TPS65218_CONFIG1_GPO2_BUF,
						   TPS65218_PROTECT_L1);
		if (param == PIN_CONFIG_DRIVE_PUSH_PULL)
			return tps65218_set_bits(tps65218,
						 TPS65218_REG_CONFIG1,
						 TPS65218_CONFIG1_GPO2_BUF,
						 TPS65218_CONFIG1_GPO2_BUF,
						 TPS65218_PROTECT_L1);
		return -ENOTSUPP;
	default:
		break;
	}
	return -ENOTSUPP;
}

static const struct gpio_chip template_chip = {
	.label			= "gpio-tps65218",
	.owner			= THIS_MODULE,
	.request		= tps65218_gpio_request,
	.direction_output	= tps65218_gpio_output,
	.direction_input	= tps65218_gpio_input,
	.get			= tps65218_gpio_get,
	.set			= tps65218_gpio_set,
	.set_config		= tps65218_gpio_set_config,
	.can_sleep		= true,
	.ngpio			= 3,
	.base			= -1,
};

static int tps65218_gpio_probe(struct platform_device *pdev)
{
	struct tps65218 *tps65218 = dev_get_drvdata(pdev->dev.parent);
	struct tps65218_gpio *tps65218_gpio;

	tps65218_gpio = devm_kzalloc(&pdev->dev, sizeof(*tps65218_gpio),
				     GFP_KERNEL);
	if (!tps65218_gpio)
		return -ENOMEM;

	tps65218_gpio->tps65218 = tps65218;
	tps65218_gpio->gpio_chip = template_chip;
	tps65218_gpio->gpio_chip.parent = &pdev->dev;

	return devm_gpiochip_add_data(&pdev->dev, &tps65218_gpio->gpio_chip,
				      tps65218_gpio);
}

static const struct of_device_id tps65218_dt_match[] = {
	{ .compatible = "ti,tps65218-gpio" },
	{  }
};
MODULE_DEVICE_TABLE(of, tps65218_dt_match);

static const struct platform_device_id tps65218_gpio_id_table[] = {
	{ "tps65218-gpio", },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(platform, tps65218_gpio_id_table);

static struct platform_driver tps65218_gpio_driver = {
	.driver = {
		.name = "tps65218-gpio",
		.of_match_table = tps65218_dt_match,
	},
	.probe = tps65218_gpio_probe,
	.id_table = tps65218_gpio_id_table,
};

module_platform_driver(tps65218_gpio_driver);

MODULE_AUTHOR("Nicolas Saenz Julienne <nicolassaenzj@gmail.com>");
MODULE_DESCRIPTION("GPO interface for TPS65218 PMICs");
MODULE_LICENSE("GPL v2");
