/*
 * linux/arch/arm/mach-w90p910/gpio.c
 *
 * Generic w90p910 GPIO handling
 *
 *  Wan ZongShun <mcuos.com@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/clk.h>
#include <linux/errno.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/debugfs.h>
#include <linux/seq_file.h>
#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/io.h>
#include <linux/gpio.h>

#include <mach/hardware.h>

#define GPIO_BASE 		(W90X900_VA_GPIO)
#define GPIO_DIR		(0x04)
#define GPIO_OUT		(0x08)
#define GPIO_IN			(0x0C)
#define GROUPINERV		(0x10)
#define GPIO_GPIO(Nb)		(0x00000001 << (Nb))
#define to_w90p910_gpio_chip(c) container_of(c, struct w90p910_gpio_chip, chip)

#define W90P910_GPIO_CHIP(name, base_gpio, nr_gpio)			\
	{								\
		.chip = {						\
			.label		  = name,			\
			.direction_input  = w90p910_dir_input,		\
			.direction_output = w90p910_dir_output,		\
			.get		  = w90p910_gpio_get,		\
			.set		  = w90p910_gpio_set,		\
			.base		  = base_gpio,			\
			.ngpio		  = nr_gpio,			\
		}							\
	}

struct w90p910_gpio_chip {
	struct gpio_chip	chip;
	void __iomem		*regbase;	/* Base of group register*/
	spinlock_t 		gpio_lock;
};

static int w90p910_gpio_get(struct gpio_chip *chip, unsigned offset)
{
	struct w90p910_gpio_chip *w90p910_gpio = to_w90p910_gpio_chip(chip);
	void __iomem *pio = w90p910_gpio->regbase + GPIO_IN;
	unsigned int regval;

	regval = __raw_readl(pio);
	regval &= GPIO_GPIO(offset);

	return (regval != 0);
}

static void w90p910_gpio_set(struct gpio_chip *chip, unsigned offset, int val)
{
	struct w90p910_gpio_chip *w90p910_gpio = to_w90p910_gpio_chip(chip);
	void __iomem *pio = w90p910_gpio->regbase + GPIO_OUT;
	unsigned int regval;
	unsigned long flags;

	spin_lock_irqsave(&w90p910_gpio->gpio_lock, flags);

	regval = __raw_readl(pio);

	if (val)
		regval |= GPIO_GPIO(offset);
	else
		regval &= ~GPIO_GPIO(offset);

	__raw_writel(regval, pio);

	spin_unlock_irqrestore(&w90p910_gpio->gpio_lock, flags);
}

static int w90p910_dir_input(struct gpio_chip *chip, unsigned offset)
{
	struct w90p910_gpio_chip *w90p910_gpio = to_w90p910_gpio_chip(chip);
	void __iomem *pio = w90p910_gpio->regbase + GPIO_DIR;
	unsigned int regval;
	unsigned long flags;

	spin_lock_irqsave(&w90p910_gpio->gpio_lock, flags);

	regval = __raw_readl(pio);
	regval &= ~GPIO_GPIO(offset);
	__raw_writel(regval, pio);

	spin_unlock_irqrestore(&w90p910_gpio->gpio_lock, flags);

	return 0;
}

static int w90p910_dir_output(struct gpio_chip *chip, unsigned offset, int val)
{
	struct w90p910_gpio_chip *w90p910_gpio = to_w90p910_gpio_chip(chip);
	void __iomem *outreg = w90p910_gpio->regbase + GPIO_OUT;
	void __iomem *pio = w90p910_gpio->regbase + GPIO_DIR;
	unsigned int regval;
	unsigned long flags;

	spin_lock_irqsave(&w90p910_gpio->gpio_lock, flags);

	regval = __raw_readl(pio);
	regval |= GPIO_GPIO(offset);
	__raw_writel(regval, pio);

	regval = __raw_readl(outreg);

	if (val)
		regval |= GPIO_GPIO(offset);
	else
		regval &= ~GPIO_GPIO(offset);

	__raw_writel(regval, outreg);

	spin_unlock_irqrestore(&w90p910_gpio->gpio_lock, flags);

	return 0;
}

static struct w90p910_gpio_chip w90p910_gpio[] = {
	W90P910_GPIO_CHIP("GROUPC", 0, 16),
	W90P910_GPIO_CHIP("GROUPD", 16, 10),
	W90P910_GPIO_CHIP("GROUPE", 26, 14),
	W90P910_GPIO_CHIP("GROUPF", 40, 10),
	W90P910_GPIO_CHIP("GROUPG", 50, 17),
	W90P910_GPIO_CHIP("GROUPH", 67, 8),
	W90P910_GPIO_CHIP("GROUPI", 75, 17),
};

void __init w90p910_init_gpio(int nr_group)
{
	unsigned	i;
	struct w90p910_gpio_chip *gpio_chip;

	for (i = 0; i < nr_group; i++) {
		gpio_chip = &w90p910_gpio[i];
		spin_lock_init(&gpio_chip->gpio_lock);
		gpio_chip->regbase = GPIO_BASE + i * GROUPINERV;
		gpiochip_add(&gpio_chip->chip);
	}
}
