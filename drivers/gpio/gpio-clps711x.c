/*
 *  CLPS711X GPIO driver
 *
 *  Copyright (C) 2012 Alexander Shiyan <shc_work@mail.ru>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <linux/io.h>
#include <linux/slab.h>
#include <linux/gpio.h>
#include <linux/module.h>
#include <linux/spinlock.h>
#include <linux/platform_device.h>

#include <mach/hardware.h>

#define CLPS711X_GPIO_PORTS	5
#define CLPS711X_GPIO_NAME	"gpio-clps711x"

struct clps711x_gpio {
	struct gpio_chip	chip[CLPS711X_GPIO_PORTS];
	spinlock_t		lock;
};

static void __iomem *clps711x_ports[] = {
	CLPS711X_VIRT_BASE + PADR,
	CLPS711X_VIRT_BASE + PBDR,
	CLPS711X_VIRT_BASE + PCDR,
	CLPS711X_VIRT_BASE + PDDR,
	CLPS711X_VIRT_BASE + PEDR,
};

static void __iomem *clps711x_pdirs[] = {
	CLPS711X_VIRT_BASE + PADDR,
	CLPS711X_VIRT_BASE + PBDDR,
	CLPS711X_VIRT_BASE + PCDDR,
	CLPS711X_VIRT_BASE + PDDDR,
	CLPS711X_VIRT_BASE + PEDDR,
};

#define clps711x_port(x)	clps711x_ports[x->base / 8]
#define clps711x_pdir(x)	clps711x_pdirs[x->base / 8]

static int gpio_clps711x_get(struct gpio_chip *chip, unsigned offset)
{
	return !!(readb(clps711x_port(chip)) & (1 << offset));
}

static void gpio_clps711x_set(struct gpio_chip *chip, unsigned offset,
			      int value)
{
	int tmp;
	unsigned long flags;
	struct clps711x_gpio *gpio = dev_get_drvdata(chip->dev);

	spin_lock_irqsave(&gpio->lock, flags);
	tmp = readb(clps711x_port(chip)) & ~(1 << offset);
	if (value)
		tmp |= 1 << offset;
	writeb(tmp, clps711x_port(chip));
	spin_unlock_irqrestore(&gpio->lock, flags);
}

static int gpio_clps711x_dir_in(struct gpio_chip *chip, unsigned offset)
{
	int tmp;
	unsigned long flags;
	struct clps711x_gpio *gpio = dev_get_drvdata(chip->dev);

	spin_lock_irqsave(&gpio->lock, flags);
	tmp = readb(clps711x_pdir(chip)) & ~(1 << offset);
	writeb(tmp, clps711x_pdir(chip));
	spin_unlock_irqrestore(&gpio->lock, flags);

	return 0;
}

static int gpio_clps711x_dir_out(struct gpio_chip *chip, unsigned offset,
				 int value)
{
	int tmp;
	unsigned long flags;
	struct clps711x_gpio *gpio = dev_get_drvdata(chip->dev);

	spin_lock_irqsave(&gpio->lock, flags);
	tmp = readb(clps711x_pdir(chip)) | (1 << offset);
	writeb(tmp, clps711x_pdir(chip));
	tmp = readb(clps711x_port(chip)) & ~(1 << offset);
	if (value)
		tmp |= 1 << offset;
	writeb(tmp, clps711x_port(chip));
	spin_unlock_irqrestore(&gpio->lock, flags);

	return 0;
}

static int gpio_clps711x_dir_in_inv(struct gpio_chip *chip, unsigned offset)
{
	int tmp;
	unsigned long flags;
	struct clps711x_gpio *gpio = dev_get_drvdata(chip->dev);

	spin_lock_irqsave(&gpio->lock, flags);
	tmp = readb(clps711x_pdir(chip)) | (1 << offset);
	writeb(tmp, clps711x_pdir(chip));
	spin_unlock_irqrestore(&gpio->lock, flags);

	return 0;
}

static int gpio_clps711x_dir_out_inv(struct gpio_chip *chip, unsigned offset,
				     int value)
{
	int tmp;
	unsigned long flags;
	struct clps711x_gpio *gpio = dev_get_drvdata(chip->dev);

	spin_lock_irqsave(&gpio->lock, flags);
	tmp = readb(clps711x_pdir(chip)) & ~(1 << offset);
	writeb(tmp, clps711x_pdir(chip));
	tmp = readb(clps711x_port(chip)) & ~(1 << offset);
	if (value)
		tmp |= 1 << offset;
	writeb(tmp, clps711x_port(chip));
	spin_unlock_irqrestore(&gpio->lock, flags);

	return 0;
}

static struct {
	char	*name;
	int	nr;
	int	inv_dir;
} clps711x_gpio_ports[] __initconst = {
	{ "PORTA", 8, 0, },
	{ "PORTB", 8, 0, },
	{ "PORTC", 8, 0, },
	{ "PORTD", 8, 1, },
	{ "PORTE", 3, 0, },
};

static int __init gpio_clps711x_init(void)
{
	int i;
	struct platform_device *pdev;
	struct clps711x_gpio *gpio;

	pdev = platform_device_alloc(CLPS711X_GPIO_NAME, 0);
	if (!pdev) {
		pr_err("Cannot create platform device: %s\n",
		       CLPS711X_GPIO_NAME);
		return -ENOMEM;
	}

	platform_device_add(pdev);

	gpio = devm_kzalloc(&pdev->dev, sizeof(struct clps711x_gpio),
			    GFP_KERNEL);
	if (!gpio) {
		dev_err(&pdev->dev, "GPIO allocating memory error\n");
		platform_device_unregister(pdev);
		return -ENOMEM;
	}

	platform_set_drvdata(pdev, gpio);

	spin_lock_init(&gpio->lock);

	for (i = 0; i < CLPS711X_GPIO_PORTS; i++) {
		gpio->chip[i].owner		= THIS_MODULE;
		gpio->chip[i].dev		= &pdev->dev;
		gpio->chip[i].label		= clps711x_gpio_ports[i].name;
		gpio->chip[i].base		= i * 8;
		gpio->chip[i].ngpio		= clps711x_gpio_ports[i].nr;
		gpio->chip[i].get		= gpio_clps711x_get;
		gpio->chip[i].set		= gpio_clps711x_set;
		if (!clps711x_gpio_ports[i].inv_dir) {
			gpio->chip[i].direction_input = gpio_clps711x_dir_in;
			gpio->chip[i].direction_output = gpio_clps711x_dir_out;
		} else {
			gpio->chip[i].direction_input = gpio_clps711x_dir_in_inv;
			gpio->chip[i].direction_output = gpio_clps711x_dir_out_inv;
		}
		WARN_ON(gpiochip_add(&gpio->chip[i]));
	}

	dev_info(&pdev->dev, "GPIO driver initialized\n");

	return 0;
}
arch_initcall(gpio_clps711x_init);

MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Alexander Shiyan <shc_work@mail.ru>");
MODULE_DESCRIPTION("CLPS711X GPIO driver");
