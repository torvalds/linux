/*
 * GPIO Driver for Dialog DA9055 PMICs.
 *
 * Copyright(c) 2012 Dialog Semiconductor Ltd.
 *
 * Author: David Dajun Chen <dchen@diasemi.com>
 *
 *  This program is free software; you can redistribute  it and/or modify it
 *  under  the terms of  the GNU General  Public License as published by the
 *  Free Software Foundation;  either version 2 of the  License, or (at your
 *  option) any later version.
 *
 */
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/gpio.h>

#include <linux/mfd/da9055/core.h>
#include <linux/mfd/da9055/reg.h>
#include <linux/mfd/da9055/pdata.h>

#define DA9055_VDD_IO			0x0
#define DA9055_PUSH_PULL		0x3
#define DA9055_ACT_LOW			0x0
#define DA9055_GPI			0x1
#define DA9055_PORT_MASK		0x3
#define DA9055_PORT_SHIFT(offset)	(4 * (offset % 2))

#define DA9055_INPUT			DA9055_GPI
#define DA9055_OUTPUT			DA9055_PUSH_PULL
#define DA9055_IRQ_GPI0			3

struct da9055_gpio {
	struct da9055 *da9055;
	struct gpio_chip gp;
};

static inline struct da9055_gpio *to_da9055_gpio(struct gpio_chip *chip)
{
	return container_of(chip, struct da9055_gpio, gp);
}

static int da9055_gpio_get(struct gpio_chip *gc, unsigned offset)
{
	struct da9055_gpio *gpio = to_da9055_gpio(gc);
	int gpio_direction = 0;
	int ret;

	/* Get GPIO direction */
	ret = da9055_reg_read(gpio->da9055, (offset >> 1) + DA9055_REG_GPIO0_1);
	if (ret < 0)
		return ret;

	gpio_direction = ret & (DA9055_PORT_MASK) << DA9055_PORT_SHIFT(offset);
	gpio_direction >>= DA9055_PORT_SHIFT(offset);
	switch (gpio_direction) {
	case DA9055_INPUT:
		ret = da9055_reg_read(gpio->da9055, DA9055_REG_STATUS_B);
		if (ret < 0)
			return ret;
		break;
	case DA9055_OUTPUT:
		ret = da9055_reg_read(gpio->da9055, DA9055_REG_GPIO_MODE0_2);
		if (ret < 0)
			return ret;
	}

	return ret & (1 << offset);

}

static void da9055_gpio_set(struct gpio_chip *gc, unsigned offset, int value)
{
	struct da9055_gpio *gpio = to_da9055_gpio(gc);

	da9055_reg_update(gpio->da9055,
			DA9055_REG_GPIO_MODE0_2,
			1 << offset,
			value << offset);
}

static int da9055_gpio_direction_input(struct gpio_chip *gc, unsigned offset)
{
	struct da9055_gpio *gpio = to_da9055_gpio(gc);
	unsigned char reg_byte;

	reg_byte = (DA9055_ACT_LOW | DA9055_GPI)
				<< DA9055_PORT_SHIFT(offset);

	return da9055_reg_update(gpio->da9055, (offset >> 1) +
				DA9055_REG_GPIO0_1,
				DA9055_PORT_MASK <<
				DA9055_PORT_SHIFT(offset),
				reg_byte);
}

static int da9055_gpio_direction_output(struct gpio_chip *gc,
					unsigned offset, int value)
{
	struct da9055_gpio *gpio = to_da9055_gpio(gc);
	unsigned char reg_byte;
	int ret;

	reg_byte = (DA9055_VDD_IO | DA9055_PUSH_PULL)
					<< DA9055_PORT_SHIFT(offset);

	ret = da9055_reg_update(gpio->da9055, (offset >> 1) +
				DA9055_REG_GPIO0_1,
				DA9055_PORT_MASK <<
				DA9055_PORT_SHIFT(offset),
				reg_byte);
	if (ret < 0)
		return ret;

	da9055_gpio_set(gc, offset, value);

	return 0;
}

static int da9055_gpio_to_irq(struct gpio_chip *gc, u32 offset)
{
	struct da9055_gpio *gpio = to_da9055_gpio(gc);
	struct da9055 *da9055 = gpio->da9055;

	return regmap_irq_get_virq(da9055->irq_data,
				  DA9055_IRQ_GPI0 + offset);
}

static struct gpio_chip reference_gp __devinitdata = {
	.label = "da9055-gpio",
	.owner = THIS_MODULE,
	.get = da9055_gpio_get,
	.set = da9055_gpio_set,
	.direction_input = da9055_gpio_direction_input,
	.direction_output = da9055_gpio_direction_output,
	.to_irq = da9055_gpio_to_irq,
	.can_sleep = 1,
	.ngpio = 3,
	.base = -1,
};

static int __devinit da9055_gpio_probe(struct platform_device *pdev)
{
	struct da9055_gpio *gpio;
	struct da9055_pdata *pdata;
	int ret;

	gpio = devm_kzalloc(&pdev->dev, sizeof(*gpio), GFP_KERNEL);
	if (gpio == NULL)
		return -ENOMEM;

	gpio->da9055 = dev_get_drvdata(pdev->dev.parent);
	pdata = gpio->da9055->dev->platform_data;

	gpio->gp = reference_gp;
	if (pdata && pdata->gpio_base)
		gpio->gp.base = pdata->gpio_base;

	ret = gpiochip_add(&gpio->gp);
	if (ret < 0) {
		dev_err(&pdev->dev, "Could not register gpiochip, %d\n", ret);
		goto err_mem;
	}

	platform_set_drvdata(pdev, gpio);

	return 0;

err_mem:
	return ret;
}

static int __devexit da9055_gpio_remove(struct platform_device *pdev)
{
	struct da9055_gpio *gpio = platform_get_drvdata(pdev);

	return gpiochip_remove(&gpio->gp);
}

static struct platform_driver da9055_gpio_driver = {
	.probe = da9055_gpio_probe,
	.remove = __devexit_p(da9055_gpio_remove),
	.driver = {
		.name	= "da9055-gpio",
		.owner	= THIS_MODULE,
	},
};

static int __init da9055_gpio_init(void)
{
	return platform_driver_register(&da9055_gpio_driver);
}
subsys_initcall(da9055_gpio_init);

static void __exit da9055_gpio_exit(void)
{
	platform_driver_unregister(&da9055_gpio_driver);
}
module_exit(da9055_gpio_exit);

MODULE_AUTHOR("David Dajun Chen <dchen@diasemi.com>");
MODULE_DESCRIPTION("DA9055 GPIO Device Driver");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:da9055-gpio");
