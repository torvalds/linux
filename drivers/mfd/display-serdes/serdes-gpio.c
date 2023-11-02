// SPDX-License-Identifier: GPL-2.0+
/*
 * gpiolib support for different serdes chip
 *
 * Copyright (c) 2023-2028 Rockchip Electronics Co. Ltd.
 *
 * Author: luowei <lw@rock-chips.com>
 *
 */

#include "core.h"

static int serdes_gpio_direction_in(struct gpio_chip *chip, unsigned int offset)
{
	struct serdes_gpio *serdes_gpio = gpiochip_get_data(chip);
	struct serdes *serdes = serdes_gpio->parent->parent;
	int ret = 0;

	if (serdes->chip_data->gpio_ops->direction_input)
		ret = serdes->chip_data->gpio_ops->direction_input(serdes, offset);

	SERDES_DBG_MFD("%s: %s %s gpio=%d\n", __func__, dev_name(serdes->dev),
		       serdes->chip_data->name, offset);
	return ret;
}

static int serdes_gpio_get(struct gpio_chip *chip, unsigned int offset)
{
	struct serdes_gpio *serdes_gpio = gpiochip_get_data(chip);
	struct serdes *serdes = serdes_gpio->parent->parent;
	int ret = 0;

	if (serdes->chip_data->gpio_ops->get_level)
		ret = serdes->chip_data->gpio_ops->get_level(serdes, offset);

	SERDES_DBG_MFD("%s: %s %s gpio=%d\n", __func__, dev_name(serdes->dev),
		       serdes->chip_data->name, offset);
	return ret;
}

static void serdes_gpio_set(struct gpio_chip *chip, unsigned int offset, int value)
{
	struct serdes_gpio *serdes_gpio = gpiochip_get_data(chip);
	struct serdes *serdes = serdes_gpio->parent->parent;
	int ret = 0;

	if (serdes->chip_data->gpio_ops->set_level)
		ret = serdes->chip_data->gpio_ops->set_level(serdes, offset, value);

	SERDES_DBG_MFD("%s: %s %s gpio=%d,val=%d\n", __func__, dev_name(serdes->dev),
		       serdes->chip_data->name, offset, value);
}

static int serdes_gpio_direction_out(struct gpio_chip *chip,
				     unsigned int offset, int value)
{
	struct serdes_gpio *serdes_gpio = gpiochip_get_data(chip);
	struct serdes *serdes = serdes_gpio->parent->parent;
	int ret = 0;

	if (serdes->chip_data->gpio_ops->direction_output)
		ret = serdes->chip_data->gpio_ops->direction_output(serdes, offset, value);

	SERDES_DBG_MFD("%s: %s %s gpio=%d,val=%d\n", __func__, dev_name(serdes->dev),
		       serdes->chip_data->name, offset, value);
	return ret;
}

static int serdes_gpio_to_irq(struct gpio_chip *chip, unsigned int offset)
{
	struct serdes_gpio *serdes_gpio = gpiochip_get_data(chip);
	struct serdes *serdes = serdes_gpio->parent->parent;
	int ret = 0;

	if (serdes->chip_data->gpio_ops->to_irq)
		ret = serdes->chip_data->gpio_ops->to_irq(serdes, offset);

	SERDES_DBG_MFD("%s: %s %s gpio=%d\n", __func__, dev_name(serdes->dev),
		       serdes->chip_data->name, offset);
	return ret;
}

static int serdes_set_config(struct gpio_chip *chip, unsigned int offset,
			     unsigned long config)
{
	struct serdes_gpio *serdes_gpio = gpiochip_get_data(chip);
	struct serdes *serdes = serdes_gpio->parent->parent;
	//int param = pinconf_to_config_param(config);
	int ret = 0;
	//int gpio = offset;

	if (serdes->chip_data->gpio_ops->set_config)
		ret = serdes->chip_data->gpio_ops->set_config(serdes, offset, config);

	SERDES_DBG_MFD("%s: %s %s gpio=%d,config=%d\n", __func__,
		       dev_name(serdes->dev),
		       serdes->chip_data->name, offset, pinconf_to_config_param(config));
	return ret;
}

#ifdef CONFIG_DEBUG_FS
static void serdes_gpio_dbg_show(struct seq_file *s, struct gpio_chip *chip)
{
	struct serdes_gpio *serdes_gpio = gpiochip_get_data(chip);
	struct serdes *serdes = serdes_gpio->parent->parent;
	int i = 0;
	int ret = 0;

	for (i = 0; i < chip->ngpio; i++) {
		int gpio = i + chip->base;
		const char *label, *level;

		/* We report the GPIO even if it's not requested since
		 * we're also reporting things like alternate
		 * functions which apply even when the GPIO is not in
		 * use as a GPIO.
		 */
		label = gpiochip_is_requested(chip, i);
		if (!label)
			label = "Unrequested";

		seq_printf(s, " %s-gpio-%02d ", label, gpio);

		if (serdes->chip_data->gpio_ops->get_level)
			ret = serdes->chip_data->gpio_ops->get_level(serdes, i);
		switch (ret) {
		case SERDES_GPIO_LEVEL_HIGH:
			level = "level-high";
			break;
		case SERDES_GPIO_LEVEL_LOW:
			level = "level-low";
			break;
		default:
			level = "invalid level";
			break;
		}

		seq_printf(s, " %s\n", level);
	}
}
#else
#define serdes_gpio_dbg_show NULL
#endif

static const struct gpio_chip serdes_gpio_chip = {
	.owner			= THIS_MODULE,
	.request = gpiochip_generic_request,
	.free = gpiochip_generic_free,
	.direction_input	= serdes_gpio_direction_in,
	.direction_output	= serdes_gpio_direction_out,
	.get			= serdes_gpio_get,
	.set			= serdes_gpio_set,
	.set_config		= serdes_set_config,
	.to_irq			= serdes_gpio_to_irq,
	.dbg_show		= serdes_gpio_dbg_show,
	.can_sleep		= true,
};

static int serdes_gpio_probe(struct platform_device *pdev)
{
	struct serdes_pinctrl *serdes_pinctrl = dev_get_drvdata(pdev->dev.parent);
	struct serdes *serdes = serdes_pinctrl->parent;
	struct serdes_chip_data *chip_data = serdes->chip_data;
	struct device *dev = &pdev->dev;
	struct serdes_gpio *serdes_gpio;
	const char *list_name = "gpio-ranges";
	struct of_phandle_args of_args;
	int ret;

	serdes_gpio = devm_kzalloc(&pdev->dev, sizeof(*serdes_gpio),
				   GFP_KERNEL);
	if (serdes_gpio == NULL)
		return -ENOMEM;

	ret = of_parse_phandle_with_fixed_args(dev->of_node, list_name, 3, 0, &of_args);
	if (ret) {
		dev_err(dev, "Unable to parse %s list property\n",
			list_name);
		return ret;
	}

	serdes_pinctrl->gpio = serdes_gpio;
	serdes_gpio->dev = dev;
	serdes_gpio->parent = serdes_pinctrl;
	serdes_gpio->gpio_chip = serdes_gpio_chip;
	serdes_gpio->gpio_chip.parent = pdev->dev.parent;
	if (of_args.args[2]) {
		serdes_gpio->gpio_chip.base = of_args.args[1];
		serdes_gpio->gpio_chip.ngpio = of_args.args[2];
	} else {
		serdes_gpio->gpio_chip.base = -1;
		serdes_gpio->gpio_chip.ngpio = 8;
	}
#ifdef CONFIG_OF_GPIO
	serdes_gpio->gpio_chip.of_node = serdes_gpio->dev->of_node;
#endif
	serdes_gpio->gpio_chip.label = kasprintf(GFP_KERNEL, "%s-gpio", chip_data->name);

	/* Add gpiochip */
	ret = devm_gpiochip_add_data(&pdev->dev, &serdes_gpio->gpio_chip,
				     serdes_gpio);
	if (ret < 0) {
		dev_err(&pdev->dev, "Could not register serdes gpiochip, %d\n", ret);
		return ret;
	}

	platform_set_drvdata(pdev, serdes_gpio);

	dev_info(serdes_gpio->dev->parent->parent,
		 "%s serdes_gpio_probe successful, base=%d, ngpio=%d\n",
		 serdes->chip_data->name,
		 serdes_gpio->gpio_chip.base, serdes_gpio->gpio_chip.ngpio);

	return ret;
}

static const struct of_device_id serdes_gpio_of_match[] = {
	{ .compatible = "rohm,bu18tl82-gpio", },
	{ .compatible = "rohm,bu18rl82-gpio", },
	{ .compatible = "maxim,max96745-gpio", },
	{ .compatible = "maxim,max96752-gpio", },
	{ .compatible = "maxim,max96755-gpio", },
	{ .compatible = "maxim,max96772-gpio", },
	{ .compatible = "maxim,max96789-gpio", },
	{ .compatible = "rockchip,rkx111-gpio", },
	{ .compatible = "rockchip,rkx121-gpio", },
	{ .compatible = "novo,nca9539-gpio", },
	{ }
};

static struct platform_driver serdes_gpio_driver = {
	.driver = {
		.name = "serdes-gpio",
		.of_match_table = of_match_ptr(serdes_gpio_of_match),
	},
	.probe = serdes_gpio_probe,
};

static int __init serdes_gpio_init(void)
{
	return platform_driver_register(&serdes_gpio_driver);
}
device_initcall(serdes_gpio_init);

static void __exit serdes_gpio_exit(void)
{
	platform_driver_unregister(&serdes_gpio_driver);
}
module_exit(serdes_gpio_exit);

MODULE_AUTHOR("Luo Wei <lw@rock-chips.com>");
MODULE_DESCRIPTION("display bridge interface for different serdes");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:serdes-bridge");
