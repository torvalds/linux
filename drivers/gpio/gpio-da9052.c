/*
 * GPIO Driver for Dialog DA9052 PMICs.
 *
 * Copyright(c) 2011 Dialog Semiconductor Ltd.
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
#include <linux/fs.h>
#include <linux/uaccess.h>
#include <linux/platform_device.h>
#include <linux/gpio.h>
#include <linux/syscalls.h>
#include <linux/seq_file.h>

#include <linux/mfd/da9052/da9052.h>
#include <linux/mfd/da9052/reg.h>
#include <linux/mfd/da9052/pdata.h>

#define DA9052_INPUT				1
#define DA9052_OUTPUT_OPENDRAIN		2
#define DA9052_OUTPUT_PUSHPULL			3

#define DA9052_SUPPLY_VDD_IO1			0

#define DA9052_DEBOUNCING_OFF			0
#define DA9052_DEBOUNCING_ON			1

#define DA9052_OUTPUT_LOWLEVEL			0

#define DA9052_ACTIVE_LOW			0
#define DA9052_ACTIVE_HIGH			1

#define DA9052_GPIO_MAX_PORTS_PER_REGISTER	8
#define DA9052_GPIO_SHIFT_COUNT(no)		(no%8)
#define DA9052_GPIO_MASK_UPPER_NIBBLE		0xF0
#define DA9052_GPIO_MASK_LOWER_NIBBLE		0x0F
#define DA9052_GPIO_NIBBLE_SHIFT		4
#define DA9052_IRQ_GPI0			16
#define DA9052_GPIO_ODD_SHIFT			7
#define DA9052_GPIO_EVEN_SHIFT			3

struct da9052_gpio {
	struct da9052 *da9052;
	struct gpio_chip gp;
};

static inline struct da9052_gpio *to_da9052_gpio(struct gpio_chip *chip)
{
	return container_of(chip, struct da9052_gpio, gp);
}

static unsigned char da9052_gpio_port_odd(unsigned offset)
{
	return offset % 2;
}

static int da9052_gpio_get(struct gpio_chip *gc, unsigned offset)
{
	struct da9052_gpio *gpio = to_da9052_gpio(gc);
	int da9052_port_direction = 0;
	int ret;

	ret = da9052_reg_read(gpio->da9052,
			      DA9052_GPIO_0_1_REG + (offset >> 1));
	if (ret < 0)
		return ret;

	if (da9052_gpio_port_odd(offset)) {
		da9052_port_direction = ret & DA9052_GPIO_ODD_PORT_PIN;
		da9052_port_direction >>= 4;
	} else {
		da9052_port_direction = ret & DA9052_GPIO_EVEN_PORT_PIN;
	}

	switch (da9052_port_direction) {
	case DA9052_INPUT:
		if (offset < DA9052_GPIO_MAX_PORTS_PER_REGISTER)
			ret = da9052_reg_read(gpio->da9052,
					      DA9052_STATUS_C_REG);
		else
			ret = da9052_reg_read(gpio->da9052,
					      DA9052_STATUS_D_REG);
		if (ret < 0)
			return ret;
		if (ret & (1 << DA9052_GPIO_SHIFT_COUNT(offset)))
			return 1;
		else
			return 0;
	case DA9052_OUTPUT_PUSHPULL:
		if (da9052_gpio_port_odd(offset))
			return ret & DA9052_GPIO_ODD_PORT_MODE;
		else
			return ret & DA9052_GPIO_EVEN_PORT_MODE;
	default:
		return -EINVAL;
	}
}

static void da9052_gpio_set(struct gpio_chip *gc, unsigned offset, int value)
{
	struct da9052_gpio *gpio = to_da9052_gpio(gc);
	int ret;

	if (da9052_gpio_port_odd(offset)) {
			ret = da9052_reg_update(gpio->da9052, (offset >> 1) +
						DA9052_GPIO_0_1_REG,
						DA9052_GPIO_ODD_PORT_MODE,
						value << DA9052_GPIO_ODD_SHIFT);
			if (ret != 0)
				dev_err(gpio->da9052->dev,
					"Failed to updated gpio odd reg,%d",
					ret);
	} else {
			ret = da9052_reg_update(gpio->da9052, (offset >> 1) +
						DA9052_GPIO_0_1_REG,
						DA9052_GPIO_EVEN_PORT_MODE,
						value << DA9052_GPIO_EVEN_SHIFT);
			if (ret != 0)
				dev_err(gpio->da9052->dev,
					"Failed to updated gpio even reg,%d",
					ret);
	}
}

static int da9052_gpio_direction_input(struct gpio_chip *gc, unsigned offset)
{
	struct da9052_gpio *gpio = to_da9052_gpio(gc);
	unsigned char register_value;
	int ret;

	/* Format: function - 2 bits type - 1 bit mode - 1 bit */
	register_value = DA9052_INPUT | DA9052_ACTIVE_LOW << 2 |
			 DA9052_DEBOUNCING_ON << 3;

	if (da9052_gpio_port_odd(offset))
		ret = da9052_reg_update(gpio->da9052, (offset >> 1) +
					DA9052_GPIO_0_1_REG,
					DA9052_GPIO_MASK_UPPER_NIBBLE,
					(register_value <<
					DA9052_GPIO_NIBBLE_SHIFT));
	else
		ret = da9052_reg_update(gpio->da9052, (offset >> 1) +
					DA9052_GPIO_0_1_REG,
					DA9052_GPIO_MASK_LOWER_NIBBLE,
					register_value);

	return ret;
}

static int da9052_gpio_direction_output(struct gpio_chip *gc,
					unsigned offset, int value)
{
	struct da9052_gpio *gpio = to_da9052_gpio(gc);
	unsigned char register_value;
	int ret;

	/* Format: Function - 2 bits Type - 1 bit Mode - 1 bit */
	register_value = DA9052_OUTPUT_PUSHPULL | DA9052_SUPPLY_VDD_IO1 << 2 |
			 value << 3;

	if (da9052_gpio_port_odd(offset))
		ret = da9052_reg_update(gpio->da9052, (offset >> 1) +
					DA9052_GPIO_0_1_REG,
					DA9052_GPIO_MASK_UPPER_NIBBLE,
					(register_value <<
					DA9052_GPIO_NIBBLE_SHIFT));
	else
		ret = da9052_reg_update(gpio->da9052, (offset >> 1) +
					DA9052_GPIO_0_1_REG,
					DA9052_GPIO_MASK_LOWER_NIBBLE,
					register_value);

	return ret;
}

static int da9052_gpio_to_irq(struct gpio_chip *gc, u32 offset)
{
	struct da9052_gpio *gpio = to_da9052_gpio(gc);
	struct da9052 *da9052 = gpio->da9052;

	return da9052->irq_base + DA9052_IRQ_GPI0 + offset;
}

static struct gpio_chip reference_gp __devinitdata = {
	.label = "da9052-gpio",
	.owner = THIS_MODULE,
	.get = da9052_gpio_get,
	.set = da9052_gpio_set,
	.direction_input = da9052_gpio_direction_input,
	.direction_output = da9052_gpio_direction_output,
	.to_irq = da9052_gpio_to_irq,
	.can_sleep = 1,
	.ngpio = 16,
	.base = -1,
};

static int __devinit da9052_gpio_probe(struct platform_device *pdev)
{
	struct da9052_gpio *gpio;
	struct da9052_pdata *pdata;
	int ret;

	gpio = devm_kzalloc(&pdev->dev, sizeof(*gpio), GFP_KERNEL);
	if (gpio == NULL)
		return -ENOMEM;

	gpio->da9052 = dev_get_drvdata(pdev->dev.parent);
	pdata = gpio->da9052->dev->platform_data;

	gpio->gp = reference_gp;
	if (pdata && pdata->gpio_base)
		gpio->gp.base = pdata->gpio_base;

	ret = gpiochip_add(&gpio->gp);
	if (ret < 0) {
		dev_err(&pdev->dev, "Could not register gpiochip, %d\n", ret);
		return ret;
	}

	platform_set_drvdata(pdev, gpio);

	return 0;
}

static int __devexit da9052_gpio_remove(struct platform_device *pdev)
{
	struct da9052_gpio *gpio = platform_get_drvdata(pdev);

	return gpiochip_remove(&gpio->gp);
}

static struct platform_driver da9052_gpio_driver = {
	.probe = da9052_gpio_probe,
	.remove = __devexit_p(da9052_gpio_remove),
	.driver = {
		.name	= "da9052-gpio",
		.owner	= THIS_MODULE,
	},
};

module_platform_driver(da9052_gpio_driver);

MODULE_AUTHOR("David Dajun Chen <dchen@diasemi.com>");
MODULE_DESCRIPTION("DA9052 GPIO Device Driver");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:da9052-gpio");
