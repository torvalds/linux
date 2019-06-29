// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * GPIO driver for Analog Devices ADP5520 MFD PMICs
 *
 * Copyright 2009 Analog Devices Inc.
 */

#include <linux/module.h>
#include <linux/slab.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/mfd/adp5520.h>
#include <linux/gpio/driver.h>

struct adp5520_gpio {
	struct device *master;
	struct gpio_chip gpio_chip;
	unsigned char lut[ADP5520_MAXGPIOS];
	unsigned long output;
};

static int adp5520_gpio_get_value(struct gpio_chip *chip, unsigned off)
{
	struct adp5520_gpio *dev;
	uint8_t reg_val;

	dev = gpiochip_get_data(chip);

	/*
	 * There are dedicated registers for GPIO IN/OUT.
	 * Make sure we return the right value, even when configured as output
	 */

	if (test_bit(off, &dev->output))
		adp5520_read(dev->master, ADP5520_GPIO_OUT, &reg_val);
	else
		adp5520_read(dev->master, ADP5520_GPIO_IN, &reg_val);

	return !!(reg_val & dev->lut[off]);
}

static void adp5520_gpio_set_value(struct gpio_chip *chip,
		unsigned off, int val)
{
	struct adp5520_gpio *dev;
	dev = gpiochip_get_data(chip);

	if (val)
		adp5520_set_bits(dev->master, ADP5520_GPIO_OUT, dev->lut[off]);
	else
		adp5520_clr_bits(dev->master, ADP5520_GPIO_OUT, dev->lut[off]);
}

static int adp5520_gpio_direction_input(struct gpio_chip *chip, unsigned off)
{
	struct adp5520_gpio *dev;
	dev = gpiochip_get_data(chip);

	clear_bit(off, &dev->output);

	return adp5520_clr_bits(dev->master, ADP5520_GPIO_CFG_2,
				dev->lut[off]);
}

static int adp5520_gpio_direction_output(struct gpio_chip *chip,
		unsigned off, int val)
{
	struct adp5520_gpio *dev;
	int ret = 0;
	dev = gpiochip_get_data(chip);

	set_bit(off, &dev->output);

	if (val)
		ret |= adp5520_set_bits(dev->master, ADP5520_GPIO_OUT,
					dev->lut[off]);
	else
		ret |= adp5520_clr_bits(dev->master, ADP5520_GPIO_OUT,
					dev->lut[off]);

	ret |= adp5520_set_bits(dev->master, ADP5520_GPIO_CFG_2,
					dev->lut[off]);

	return ret;
}

static int adp5520_gpio_probe(struct platform_device *pdev)
{
	struct adp5520_gpio_platform_data *pdata = dev_get_platdata(&pdev->dev);
	struct adp5520_gpio *dev;
	struct gpio_chip *gc;
	int ret, i, gpios;
	unsigned char ctl_mask = 0;

	if (pdata == NULL) {
		dev_err(&pdev->dev, "missing platform data\n");
		return -ENODEV;
	}

	if (pdev->id != ID_ADP5520) {
		dev_err(&pdev->dev, "only ADP5520 supports GPIO\n");
		return -ENODEV;
	}

	dev = devm_kzalloc(&pdev->dev, sizeof(*dev), GFP_KERNEL);
	if (dev == NULL)
		return -ENOMEM;

	dev->master = pdev->dev.parent;

	for (gpios = 0, i = 0; i < ADP5520_MAXGPIOS; i++)
		if (pdata->gpio_en_mask & (1 << i))
			dev->lut[gpios++] = 1 << i;

	if (gpios < 1) {
		ret = -EINVAL;
		goto err;
	}

	gc = &dev->gpio_chip;
	gc->direction_input  = adp5520_gpio_direction_input;
	gc->direction_output = adp5520_gpio_direction_output;
	gc->get = adp5520_gpio_get_value;
	gc->set = adp5520_gpio_set_value;
	gc->can_sleep = true;

	gc->base = pdata->gpio_start;
	gc->ngpio = gpios;
	gc->label = pdev->name;
	gc->owner = THIS_MODULE;

	ret = adp5520_clr_bits(dev->master, ADP5520_GPIO_CFG_1,
		pdata->gpio_en_mask);

	if (pdata->gpio_en_mask & ADP5520_GPIO_C3)
		ctl_mask |= ADP5520_C3_MODE;

	if (pdata->gpio_en_mask & ADP5520_GPIO_R3)
		ctl_mask |= ADP5520_R3_MODE;

	if (ctl_mask)
		ret = adp5520_set_bits(dev->master, ADP5520_LED_CONTROL,
			ctl_mask);

	ret |= adp5520_set_bits(dev->master, ADP5520_GPIO_PULLUP,
		pdata->gpio_pullup_mask);

	if (ret) {
		dev_err(&pdev->dev, "failed to write\n");
		goto err;
	}

	ret = devm_gpiochip_add_data(&pdev->dev, &dev->gpio_chip, dev);
	if (ret)
		goto err;

	platform_set_drvdata(pdev, dev);
	return 0;

err:
	return ret;
}

static struct platform_driver adp5520_gpio_driver = {
	.driver	= {
		.name	= "adp5520-gpio",
	},
	.probe		= adp5520_gpio_probe,
};

module_platform_driver(adp5520_gpio_driver);

MODULE_AUTHOR("Michael Hennerich <michael.hennerich@analog.com>");
MODULE_DESCRIPTION("GPIO ADP5520 Driver");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:adp5520-gpio");
