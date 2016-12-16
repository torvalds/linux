/*
 * arch/sh/boards/mach-x3proto/gpio.c
 *
 * Renesas SH-X3 Prototype Baseboard GPIO Support.
 *
 * Copyright (C) 2010 - 2012  Paul Mundt
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 */
#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/gpio/driver.h>
#include <linux/irq.h>
#include <linux/kernel.h>
#include <linux/spinlock.h>
#include <linux/irqdomain.h>
#include <linux/io.h>
#include <mach/ilsel.h>
#include <mach/hardware.h>

#define KEYCTLR	0xb81c0000
#define KEYOUTR	0xb81c0002
#define KEYDETR 0xb81c0004

static DEFINE_SPINLOCK(x3proto_gpio_lock);
static struct irq_domain *x3proto_irq_domain;

static int x3proto_gpio_direction_input(struct gpio_chip *chip, unsigned gpio)
{
	unsigned long flags;
	unsigned int data;

	spin_lock_irqsave(&x3proto_gpio_lock, flags);
	data = __raw_readw(KEYCTLR);
	data |= (1 << gpio);
	__raw_writew(data, KEYCTLR);
	spin_unlock_irqrestore(&x3proto_gpio_lock, flags);

	return 0;
}

static int x3proto_gpio_get(struct gpio_chip *chip, unsigned gpio)
{
	return !!(__raw_readw(KEYDETR) & (1 << gpio));
}

static int x3proto_gpio_to_irq(struct gpio_chip *chip, unsigned gpio)
{
	int virq;

	if (gpio < chip->ngpio)
		virq = irq_create_mapping(x3proto_irq_domain, gpio);
	else
		virq = -ENXIO;

	return virq;
}

static void x3proto_gpio_irq_handler(struct irq_desc *desc)
{
	struct irq_data *data = irq_desc_get_irq_data(desc);
	struct irq_chip *chip = irq_data_get_irq_chip(data);
	unsigned long mask;
	int pin;

	chip->irq_mask_ack(data);

	mask = __raw_readw(KEYDETR);
	for_each_set_bit(pin, &mask, NR_BASEBOARD_GPIOS)
		generic_handle_irq(irq_linear_revmap(x3proto_irq_domain, pin));

	chip->irq_unmask(data);
}

struct gpio_chip x3proto_gpio_chip = {
	.label			= "x3proto-gpio",
	.direction_input	= x3proto_gpio_direction_input,
	.get			= x3proto_gpio_get,
	.to_irq			= x3proto_gpio_to_irq,
	.base			= -1,
	.ngpio			= NR_BASEBOARD_GPIOS,
};

static int x3proto_gpio_irq_map(struct irq_domain *domain, unsigned int virq,
				irq_hw_number_t hwirq)
{
	irq_set_chip_and_handler_name(virq, &dummy_irq_chip, handle_simple_irq,
				      "gpio");

	return 0;
}

static struct irq_domain_ops x3proto_gpio_irq_ops = {
	.map	= x3proto_gpio_irq_map,
	.xlate	= irq_domain_xlate_twocell,
};

int __init x3proto_gpio_setup(void)
{
	int ilsel, ret;

	ilsel = ilsel_enable(ILSEL_KEY);
	if (unlikely(ilsel < 0))
		return ilsel;

	ret = gpiochip_add_data(&x3proto_gpio_chip, NULL);
	if (unlikely(ret))
		goto err_gpio;

	x3proto_irq_domain = irq_domain_add_linear(NULL, NR_BASEBOARD_GPIOS,
						   &x3proto_gpio_irq_ops, NULL);
	if (unlikely(!x3proto_irq_domain))
		goto err_irq;

	pr_info("registering '%s' support, handling GPIOs %u -> %u, "
		"bound to IRQ %u\n",
		x3proto_gpio_chip.label, x3proto_gpio_chip.base,
		x3proto_gpio_chip.base + x3proto_gpio_chip.ngpio,
		ilsel);

	irq_set_chained_handler(ilsel, x3proto_gpio_irq_handler);
	irq_set_irq_wake(ilsel, 1);

	return 0;

err_irq:
	gpiochip_remove(&x3proto_gpio_chip);
	ret = 0;
err_gpio:
	synchronize_irq(ilsel);

	ilsel_disable(ILSEL_KEY);

	return ret;
}
