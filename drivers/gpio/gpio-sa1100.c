/*
 * linux/arch/arm/mach-sa1100/gpio.c
 *
 * Generic SA-1100 GPIO handling
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <linux/gpio.h>
#include <linux/init.h>
#include <linux/module.h>

#include <mach/hardware.h>
#include <mach/irqs.h>

static int sa1100_gpio_get(struct gpio_chip *chip, unsigned offset)
{
	return GPLR & GPIO_GPIO(offset);
}

static void sa1100_gpio_set(struct gpio_chip *chip, unsigned offset, int value)
{
	if (value)
		GPSR = GPIO_GPIO(offset);
	else
		GPCR = GPIO_GPIO(offset);
}

static int sa1100_direction_input(struct gpio_chip *chip, unsigned offset)
{
	unsigned long flags;

	local_irq_save(flags);
	GPDR &= ~GPIO_GPIO(offset);
	local_irq_restore(flags);
	return 0;
}

static int sa1100_direction_output(struct gpio_chip *chip, unsigned offset, int value)
{
	unsigned long flags;

	local_irq_save(flags);
	sa1100_gpio_set(chip, offset, value);
	GPDR |= GPIO_GPIO(offset);
	local_irq_restore(flags);
	return 0;
}

static int sa1100_to_irq(struct gpio_chip *chip, unsigned offset)
{
	return offset < 11 ? (IRQ_GPIO0 + offset) : (IRQ_GPIO11 - 11 + offset);
}

static struct gpio_chip sa1100_gpio_chip = {
	.label			= "gpio",
	.direction_input	= sa1100_direction_input,
	.direction_output	= sa1100_direction_output,
	.set			= sa1100_gpio_set,
	.get			= sa1100_gpio_get,
	.to_irq			= sa1100_to_irq,
	.base			= 0,
	.ngpio			= GPIO_MAX + 1,
};

void __init sa1100_init_gpio(void)
{
	gpiochip_add(&sa1100_gpio_chip);
}
