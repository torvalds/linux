/*
 * arch/arm/plat-iop/gpio.c
 * GPIO handling for Intel IOP3xx processors.
 *
 * Copyright (C) 2006 Lennert Buytenhek <buytenh@wantstofly.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or (at
 * your option) any later version.
 */

#include <linux/device.h>
#include <linux/init.h>
#include <linux/types.h>
#include <linux/errno.h>
#include <linux/gpio.h>
#include <linux/export.h>
#include <linux/platform_device.h>
#include <linux/bitops.h>
#include <linux/io.h>

#define IOP3XX_N_GPIOS	8

#define GPIO_IN			0
#define GPIO_OUT		1
#define GPIO_LOW		0
#define GPIO_HIGH		1

/* Memory base offset */
static void __iomem *base;

#define IOP3XX_GPIO_REG(reg)	(base + (reg))
#define IOP3XX_GPOE		IOP3XX_GPIO_REG(0x0000)
#define IOP3XX_GPID		IOP3XX_GPIO_REG(0x0004)
#define IOP3XX_GPOD		IOP3XX_GPIO_REG(0x0008)

static void gpio_line_config(int line, int direction)
{
	unsigned long flags;
	u32 val;

	local_irq_save(flags);
	val = readl(IOP3XX_GPOE);
	if (direction == GPIO_IN) {
		val |= BIT(line);
	} else if (direction == GPIO_OUT) {
		val &= ~BIT(line);
	}
	writel(val, IOP3XX_GPOE);
	local_irq_restore(flags);
}

static int gpio_line_get(int line)
{
	return !!(readl(IOP3XX_GPID) & BIT(line));
}

static void gpio_line_set(int line, int value)
{
	unsigned long flags;
	u32 val;

	local_irq_save(flags);
	val = readl(IOP3XX_GPOD);
	if (value == GPIO_LOW) {
		val &= ~BIT(line);
	} else if (value == GPIO_HIGH) {
		val |= BIT(line);
	}
	writel(val, IOP3XX_GPOD);
	local_irq_restore(flags);
}

static int iop3xx_gpio_direction_input(struct gpio_chip *chip, unsigned gpio)
{
	gpio_line_config(gpio, GPIO_IN);
	return 0;
}

static int iop3xx_gpio_direction_output(struct gpio_chip *chip, unsigned gpio, int level)
{
	gpio_line_set(gpio, level);
	gpio_line_config(gpio, GPIO_OUT);
	return 0;
}

static int iop3xx_gpio_get_value(struct gpio_chip *chip, unsigned gpio)
{
	return gpio_line_get(gpio);
}

static void iop3xx_gpio_set_value(struct gpio_chip *chip, unsigned gpio, int value)
{
	gpio_line_set(gpio, value);
}

static struct gpio_chip iop3xx_chip = {
	.label			= "iop3xx",
	.direction_input	= iop3xx_gpio_direction_input,
	.get			= iop3xx_gpio_get_value,
	.direction_output	= iop3xx_gpio_direction_output,
	.set			= iop3xx_gpio_set_value,
	.base			= 0,
	.ngpio			= IOP3XX_N_GPIOS,
};

static int iop3xx_gpio_probe(struct platform_device *pdev)
{
	struct resource *res;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	base = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(base))
		return PTR_ERR(base);

	return gpiochip_add(&iop3xx_chip);
}

static struct platform_driver iop3xx_gpio_driver = {
	.driver = {
		.name = "gpio-iop",
		.owner = THIS_MODULE,
	},
	.probe = iop3xx_gpio_probe,
};

static int __init iop3xx_gpio_init(void)
{
	return platform_driver_register(&iop3xx_gpio_driver);
}
arch_initcall(iop3xx_gpio_init);
