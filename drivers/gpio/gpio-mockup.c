/*
 * GPIO Testing Device Driver
 *
 * Copyright (C) 2014  Kamlakant Patel <kamlakant.patel@broadcom.com>
 * Copyright (C) 2015-2016  Bamvor Jian Zhang <bamvor.zhangjian@linaro.org>
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 *
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/gpio/driver.h>
#include <linux/platform_device.h>

#define GPIO_MOCKUP_NAME	"gpio-mockup"
#define	GPIO_MOCKUP_MAX_GC	10

enum {
	DIR_IN = 0,
	DIR_OUT,
};

/*
 * struct gpio_pin_status - structure describing a GPIO status
 * @dir:       Configures direction of gpio as "in" or "out", 0=in, 1=out
 * @value:     Configures status of the gpio as 0(low) or 1(high)
 */
struct gpio_mockup_line_status {
	int dir;
	bool value;
};

struct gpio_mockup_chip {
	struct gpio_chip gc;
	struct gpio_mockup_line_status *lines;
};

static int gpio_mockup_ranges[GPIO_MOCKUP_MAX_GC << 1];
static int gpio_mockup_params_nr;
module_param_array(gpio_mockup_ranges, int, &gpio_mockup_params_nr, 0400);

static const char gpio_mockup_name_start = 'A';

static int gpio_mockup_get(struct gpio_chip *gc, unsigned int offset)
{
	struct gpio_mockup_chip *chip = gpiochip_get_data(gc);

	return chip->lines[offset].value;
}

static void gpio_mockup_set(struct gpio_chip *gc, unsigned int offset,
			    int value)
{
	struct gpio_mockup_chip *chip = gpiochip_get_data(gc);

	chip->lines[offset].value = !!value;
}

static int gpio_mockup_dirout(struct gpio_chip *gc, unsigned int offset,
			      int value)
{
	struct gpio_mockup_chip *chip = gpiochip_get_data(gc);

	gpio_mockup_set(gc, offset, value);
	chip->lines[offset].dir = DIR_OUT;

	return 0;
}

static int gpio_mockup_dirin(struct gpio_chip *gc, unsigned int offset)
{
	struct gpio_mockup_chip *chip = gpiochip_get_data(gc);

	chip->lines[offset].dir = DIR_IN;

	return 0;
}

static int gpio_mockup_get_direction(struct gpio_chip *gc, unsigned int offset)
{
	struct gpio_mockup_chip *chip = gpiochip_get_data(gc);

	return chip->lines[offset].dir;
}

static int gpio_mockup_add(struct device *dev,
			   struct gpio_mockup_chip *chip,
			   const char *name, int base, int ngpio)
{
	struct gpio_chip *gc = &chip->gc;

	gc->base = base;
	gc->ngpio = ngpio;
	gc->label = name;
	gc->owner = THIS_MODULE;
	gc->parent = dev;
	gc->get = gpio_mockup_get;
	gc->set = gpio_mockup_set;
	gc->direction_output = gpio_mockup_dirout;
	gc->direction_input = gpio_mockup_dirin;
	gc->get_direction = gpio_mockup_get_direction;

	chip->lines = devm_kzalloc(dev, sizeof(*chip->lines) * gc->ngpio,
				   GFP_KERNEL);
	if (!chip->lines)
		return -ENOMEM;

	return devm_gpiochip_add_data(dev, &chip->gc, chip);
}

static int gpio_mockup_probe(struct platform_device *pdev)
{
	struct gpio_mockup_chip *chips;
	struct device *dev = &pdev->dev;
	int ret, i, base, ngpio;
	char *chip_name;

	if (gpio_mockup_params_nr < 2)
		return -EINVAL;

	chips = devm_kzalloc(dev,
			     sizeof(*chips) * (gpio_mockup_params_nr >> 1),
			     GFP_KERNEL);
	if (!chips)
		return -ENOMEM;

	platform_set_drvdata(pdev, chips);

	for (i = 0; i < gpio_mockup_params_nr >> 1; i++) {
		base = gpio_mockup_ranges[i * 2];

		if (base == -1)
			ngpio = gpio_mockup_ranges[i * 2 + 1];
		else
			ngpio = gpio_mockup_ranges[i * 2 + 1] - base;

		if (ngpio >= 0) {
			chip_name = devm_kasprintf(dev, GFP_KERNEL,
						   "%s-%c", GPIO_MOCKUP_NAME,
						   gpio_mockup_name_start + i);
			if (!chip_name)
				return -ENOMEM;

			ret = gpio_mockup_add(dev, &chips[i],
					      chip_name, base, ngpio);
		} else {
			ret = -1;
		}

		if (ret) {
			dev_err(dev, "gpio<%d..%d> add failed\n",
				base, base < 0 ? ngpio : base + ngpio);

			return ret;
		}

		dev_info(dev, "gpio<%d..%d> add successful!",
			 base, base + ngpio);
	}

	return 0;
}

static struct platform_driver gpio_mockup_driver = {
	.driver = {
		.name = GPIO_MOCKUP_NAME,
	},
	.probe = gpio_mockup_probe,
};

static struct platform_device *pdev;
static int __init mock_device_init(void)
{
	int err;

	pdev = platform_device_alloc(GPIO_MOCKUP_NAME, -1);
	if (!pdev)
		return -ENOMEM;

	err = platform_device_add(pdev);
	if (err) {
		platform_device_put(pdev);
		return err;
	}

	err = platform_driver_register(&gpio_mockup_driver);
	if (err) {
		platform_device_unregister(pdev);
		return err;
	}

	return 0;
}

static void __exit mock_device_exit(void)
{
	platform_driver_unregister(&gpio_mockup_driver);
	platform_device_unregister(pdev);
}

module_init(mock_device_init);
module_exit(mock_device_exit);

MODULE_AUTHOR("Kamlakant Patel <kamlakant.patel@broadcom.com>");
MODULE_AUTHOR("Bamvor Jian Zhang <bamvor.zhangjian@linaro.org>");
MODULE_DESCRIPTION("GPIO Testing driver");
MODULE_LICENSE("GPL v2");
