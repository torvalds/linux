// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Congatec Board Controller GPIO driver
 *
 * Copyright (C) 2024 Bootlin
 * Author: Thomas Richard <thomas.richard@bootlin.com>
 */

#include <linux/gpio/driver.h>
#include <linux/mfd/cgbc.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/platform_device.h>

#define CGBC_GPIO_NGPIO	14

#define CGBC_GPIO_CMD_GET	0x64
#define CGBC_GPIO_CMD_SET	0x65
#define CGBC_GPIO_CMD_DIR_GET	0x66
#define CGBC_GPIO_CMD_DIR_SET	0x67

struct cgbc_gpio_data {
	struct gpio_chip	chip;
	struct cgbc_device_data	*cgbc;
	struct mutex lock;
};

static int cgbc_gpio_cmd(struct cgbc_device_data *cgbc,
			 u8 cmd0, u8 cmd1, u8 cmd2, u8 *value)
{
	u8 cmd[3] = {cmd0, cmd1, cmd2};

	return cgbc_command(cgbc, cmd, sizeof(cmd), value, 1, NULL);
}

static int cgbc_gpio_get(struct gpio_chip *chip, unsigned int offset)
{
	struct cgbc_gpio_data *gpio = gpiochip_get_data(chip);
	struct cgbc_device_data *cgbc = gpio->cgbc;
	int ret;
	u8 val;

	scoped_guard(mutex, &gpio->lock)
		ret = cgbc_gpio_cmd(cgbc, CGBC_GPIO_CMD_GET, (offset > 7) ? 1 : 0, 0, &val);

	offset %= 8;

	if (ret)
		return ret;
	else
		return (int)(val & (u8)BIT(offset));
}

static void __cgbc_gpio_set(struct gpio_chip *chip,
			    unsigned int offset, int value)
{
	struct cgbc_gpio_data *gpio = gpiochip_get_data(chip);
	struct cgbc_device_data *cgbc = gpio->cgbc;
	u8 val;
	int ret;

	ret = cgbc_gpio_cmd(cgbc, CGBC_GPIO_CMD_GET, (offset > 7) ? 1 : 0, 0, &val);
	if (ret)
		return;

	if (value)
		val |= BIT(offset % 8);
	else
		val &= ~(BIT(offset % 8));

	cgbc_gpio_cmd(cgbc, CGBC_GPIO_CMD_SET, (offset > 7) ? 1 : 0, val, &val);
}

static void cgbc_gpio_set(struct gpio_chip *chip,
			  unsigned int offset, int value)
{
	struct cgbc_gpio_data *gpio = gpiochip_get_data(chip);

	scoped_guard(mutex, &gpio->lock)
		__cgbc_gpio_set(chip, offset, value);
}

static int cgbc_gpio_direction_set(struct gpio_chip *chip,
				   unsigned int offset, int direction)
{
	struct cgbc_gpio_data *gpio = gpiochip_get_data(chip);
	struct cgbc_device_data *cgbc = gpio->cgbc;
	int ret;
	u8 val;

	ret = cgbc_gpio_cmd(cgbc, CGBC_GPIO_CMD_DIR_GET, (offset > 7) ? 1 : 0, 0, &val);
	if (ret)
		goto end;

	if (direction == GPIO_LINE_DIRECTION_IN)
		val &= ~(BIT(offset % 8));
	else
		val |= BIT(offset % 8);

	ret = cgbc_gpio_cmd(cgbc, CGBC_GPIO_CMD_DIR_SET, (offset > 7) ? 1 : 0, val, &val);

end:
	return ret;
}

static int cgbc_gpio_direction_input(struct gpio_chip *chip,
				     unsigned int offset)
{
	struct cgbc_gpio_data *gpio = gpiochip_get_data(chip);

	guard(mutex)(&gpio->lock);
	return cgbc_gpio_direction_set(chip, offset, GPIO_LINE_DIRECTION_IN);
}

static int cgbc_gpio_direction_output(struct gpio_chip *chip,
				      unsigned int offset, int value)
{
	struct cgbc_gpio_data *gpio = gpiochip_get_data(chip);

	guard(mutex)(&gpio->lock);

	__cgbc_gpio_set(chip, offset, value);
	return cgbc_gpio_direction_set(chip, offset, GPIO_LINE_DIRECTION_OUT);
}

static int cgbc_gpio_get_direction(struct gpio_chip *chip, unsigned int offset)
{
	struct cgbc_gpio_data *gpio = gpiochip_get_data(chip);
	struct cgbc_device_data *cgbc = gpio->cgbc;
	int ret;
	u8 val;

	scoped_guard(mutex, &gpio->lock)
		ret = cgbc_gpio_cmd(cgbc, CGBC_GPIO_CMD_DIR_GET, (offset > 7) ? 1 : 0, 0, &val);

	if (ret)
		return ret;

	if (val & BIT(offset % 8))
		return GPIO_LINE_DIRECTION_OUT;
	else
		return GPIO_LINE_DIRECTION_IN;
}

static int cgbc_gpio_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct cgbc_device_data *cgbc = dev_get_drvdata(dev->parent);
	struct cgbc_gpio_data *gpio;
	struct gpio_chip *chip;
	int ret;

	gpio = devm_kzalloc(dev, sizeof(*gpio), GFP_KERNEL);
	if (!gpio)
		return -ENOMEM;

	gpio->cgbc = cgbc;

	platform_set_drvdata(pdev, gpio);

	chip = &gpio->chip;
	chip->label = dev_name(&pdev->dev);
	chip->owner = THIS_MODULE;
	chip->parent = dev;
	chip->base = -1;
	chip->direction_input = cgbc_gpio_direction_input;
	chip->direction_output = cgbc_gpio_direction_output;
	chip->get_direction = cgbc_gpio_get_direction;
	chip->get = cgbc_gpio_get;
	chip->set = cgbc_gpio_set;
	chip->ngpio = CGBC_GPIO_NGPIO;

	ret = devm_mutex_init(dev, &gpio->lock);
	if (ret)
		return ret;

	ret = devm_gpiochip_add_data(dev, chip, gpio);
	if (ret)
		return dev_err_probe(dev, ret, "Could not register GPIO chip\n");

	return 0;
}

static struct platform_driver cgbc_gpio_driver = {
	.driver = {
		.name = "cgbc-gpio",
	},
	.probe	= cgbc_gpio_probe,
};

module_platform_driver(cgbc_gpio_driver);

MODULE_DESCRIPTION("Congatec Board Controller GPIO Driver");
MODULE_AUTHOR("Thomas Richard <thomas.richard@bootlin.com>");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:cgbc-gpio");
