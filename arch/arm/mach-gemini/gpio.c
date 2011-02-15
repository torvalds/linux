/*
 * Gemini gpiochip and interrupt routines
 *
 * Copyright (C) 2008-2009 Paulius Zaleckas <paulius.zaleckas@teltonika.lt>
 *
 * Based on plat-mxc/gpio.c:
 *  MXC GPIO support. (c) 2008 Daniel Mack <daniel@caiaq.de>
 *  Copyright 2008 Juergen Beisert, kernel@pengutronix.de
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/irq.h>
#include <linux/gpio.h>

#include <mach/hardware.h>
#include <mach/irqs.h>

#define GPIO_BASE(x)		IO_ADDRESS(GEMINI_GPIO_BASE(x))

/* GPIO registers definition */
#define GPIO_DATA_OUT		0x0
#define GPIO_DATA_IN		0x4
#define GPIO_DIR		0x8
#define GPIO_DATA_SET		0x10
#define GPIO_DATA_CLR		0x14
#define GPIO_PULL_EN		0x18
#define GPIO_PULL_TYPE		0x1C
#define GPIO_INT_EN		0x20
#define GPIO_INT_STAT		0x24
#define GPIO_INT_MASK		0x2C
#define GPIO_INT_CLR		0x30
#define GPIO_INT_TYPE		0x34
#define GPIO_INT_BOTH_EDGE	0x38
#define GPIO_INT_LEVEL		0x3C
#define GPIO_DEBOUNCE_EN	0x40
#define GPIO_DEBOUNCE_PRESCALE	0x44

#define GPIO_PORT_NUM		3

static void _set_gpio_irqenable(unsigned int base, unsigned int index,
				int enable)
{
	unsigned int reg;

	reg = __raw_readl(base + GPIO_INT_EN);
	reg = (reg & (~(1 << index))) | (!!enable << index);
	__raw_writel(reg, base + GPIO_INT_EN);
}

static void gpio_ack_irq(struct irq_data *d)
{
	unsigned int gpio = irq_to_gpio(d->irq);
	unsigned int base = GPIO_BASE(gpio / 32);

	__raw_writel(1 << (gpio % 32), base + GPIO_INT_CLR);
}

static void gpio_mask_irq(struct irq_data *d)
{
	unsigned int gpio = irq_to_gpio(d->irq);
	unsigned int base = GPIO_BASE(gpio / 32);

	_set_gpio_irqenable(base, gpio % 32, 0);
}

static void gpio_unmask_irq(struct irq_data *d)
{
	unsigned int gpio = irq_to_gpio(d->irq);
	unsigned int base = GPIO_BASE(gpio / 32);

	_set_gpio_irqenable(base, gpio % 32, 1);
}

static int gpio_set_irq_type(struct irq_data *d, unsigned int type)
{
	unsigned int gpio = irq_to_gpio(d->irq);
	unsigned int gpio_mask = 1 << (gpio % 32);
	unsigned int base = GPIO_BASE(gpio / 32);
	unsigned int reg_both, reg_level, reg_type;

	reg_type = __raw_readl(base + GPIO_INT_TYPE);
	reg_level = __raw_readl(base + GPIO_INT_LEVEL);
	reg_both = __raw_readl(base + GPIO_INT_BOTH_EDGE);

	switch (type) {
	case IRQ_TYPE_EDGE_BOTH:
		reg_type &= ~gpio_mask;
		reg_both |= gpio_mask;
		break;
	case IRQ_TYPE_EDGE_RISING:
		reg_type &= ~gpio_mask;
		reg_both &= ~gpio_mask;
		reg_level &= ~gpio_mask;
		break;
	case IRQ_TYPE_EDGE_FALLING:
		reg_type &= ~gpio_mask;
		reg_both &= ~gpio_mask;
		reg_level |= gpio_mask;
		break;
	case IRQ_TYPE_LEVEL_HIGH:
		reg_type |= gpio_mask;
		reg_level &= ~gpio_mask;
		break;
	case IRQ_TYPE_LEVEL_LOW:
		reg_type |= gpio_mask;
		reg_level |= gpio_mask;
		break;
	default:
		return -EINVAL;
	}

	__raw_writel(reg_type, base + GPIO_INT_TYPE);
	__raw_writel(reg_level, base + GPIO_INT_LEVEL);
	__raw_writel(reg_both, base + GPIO_INT_BOTH_EDGE);

	gpio_ack_irq(d->irq);

	return 0;
}

static void gpio_irq_handler(unsigned int irq, struct irq_desc *desc)
{
	unsigned int gpio_irq_no, irq_stat;
	unsigned int port = (unsigned int)get_irq_data(irq);

	irq_stat = __raw_readl(GPIO_BASE(port) + GPIO_INT_STAT);

	gpio_irq_no = GPIO_IRQ_BASE + port * 32;
	for (; irq_stat != 0; irq_stat >>= 1, gpio_irq_no++) {

		if ((irq_stat & 1) == 0)
			continue;

		BUG_ON(!(irq_desc[gpio_irq_no].handle_irq));
		irq_desc[gpio_irq_no].handle_irq(gpio_irq_no,
				&irq_desc[gpio_irq_no]);
	}
}

static struct irq_chip gpio_irq_chip = {
	.name = "GPIO",
	.irq_ack = gpio_ack_irq,
	.irq_mask = gpio_mask_irq,
	.irq_unmask = gpio_unmask_irq,
	.irq_set_type = gpio_set_irq_type,
};

static void _set_gpio_direction(struct gpio_chip *chip, unsigned offset,
				int dir)
{
	unsigned int base = GPIO_BASE(offset / 32);
	unsigned int reg;

	reg = __raw_readl(base + GPIO_DIR);
	if (dir)
		reg |= 1 << (offset % 32);
	else
		reg &= ~(1 << (offset % 32));
	__raw_writel(reg, base + GPIO_DIR);
}

static void gemini_gpio_set(struct gpio_chip *chip, unsigned offset, int value)
{
	unsigned int base = GPIO_BASE(offset / 32);

	if (value)
		__raw_writel(1 << (offset % 32), base + GPIO_DATA_SET);
	else
		__raw_writel(1 << (offset % 32), base + GPIO_DATA_CLR);
}

static int gemini_gpio_get(struct gpio_chip *chip, unsigned offset)
{
	unsigned int base = GPIO_BASE(offset / 32);

	return (__raw_readl(base + GPIO_DATA_IN) >> (offset % 32)) & 1;
}

static int gemini_gpio_direction_input(struct gpio_chip *chip, unsigned offset)
{
	_set_gpio_direction(chip, offset, 0);
	return 0;
}

static int gemini_gpio_direction_output(struct gpio_chip *chip, unsigned offset,
					int value)
{
	_set_gpio_direction(chip, offset, 1);
	gemini_gpio_set(chip, offset, value);
	return 0;
}

static struct gpio_chip gemini_gpio_chip = {
	.label			= "Gemini",
	.direction_input	= gemini_gpio_direction_input,
	.get			= gemini_gpio_get,
	.direction_output	= gemini_gpio_direction_output,
	.set			= gemini_gpio_set,
	.base			= 0,
	.ngpio			= GPIO_PORT_NUM * 32,
};

void __init gemini_gpio_init(void)
{
	int i, j;

	for (i = 0; i < GPIO_PORT_NUM; i++) {
		/* disable, unmask and clear all interrupts */
		__raw_writel(0x0, GPIO_BASE(i) + GPIO_INT_EN);
		__raw_writel(0x0, GPIO_BASE(i) + GPIO_INT_MASK);
		__raw_writel(~0x0, GPIO_BASE(i) + GPIO_INT_CLR);

		for (j = GPIO_IRQ_BASE + i * 32;
		     j < GPIO_IRQ_BASE + (i + 1) * 32; j++) {
			set_irq_chip(j, &gpio_irq_chip);
			set_irq_handler(j, handle_edge_irq);
			set_irq_flags(j, IRQF_VALID);
		}

		set_irq_chained_handler(IRQ_GPIO(i), gpio_irq_handler);
		set_irq_data(IRQ_GPIO(i), (void *)i);
	}

	BUG_ON(gpiochip_add(&gemini_gpio_chip));
}
