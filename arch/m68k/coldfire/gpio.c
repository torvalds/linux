/*
 * Coldfire generic GPIO support.
 *
 * (C) Copyright 2009, Steven King <sfking@fdwdc.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/device.h>

#include <linux/io.h>
#include <asm/coldfire.h>
#include <asm/mcfsim.h>
#include <asm/mcfgpio.h>

int __mcfgpio_get_value(unsigned gpio)
{
	return mcfgpio_read(__mcfgpio_ppdr(gpio)) & mcfgpio_bit(gpio);
}
EXPORT_SYMBOL(__mcfgpio_get_value);

void __mcfgpio_set_value(unsigned gpio, int value)
{
	if (gpio < MCFGPIO_SCR_START) {
		unsigned long flags;
		MCFGPIO_PORTTYPE data;

		local_irq_save(flags);
		data = mcfgpio_read(__mcfgpio_podr(gpio));
		if (value)
			data |= mcfgpio_bit(gpio);
		else
			data &= ~mcfgpio_bit(gpio);
		mcfgpio_write(data, __mcfgpio_podr(gpio));
		local_irq_restore(flags);
	} else {
		if (value)
			mcfgpio_write(mcfgpio_bit(gpio),
					MCFGPIO_SETR_PORT(gpio));
		else
			mcfgpio_write(~mcfgpio_bit(gpio),
					MCFGPIO_CLRR_PORT(gpio));
	}
}
EXPORT_SYMBOL(__mcfgpio_set_value);

int __mcfgpio_direction_input(unsigned gpio)
{
	unsigned long flags;
	MCFGPIO_PORTTYPE dir;

	local_irq_save(flags);
	dir = mcfgpio_read(__mcfgpio_pddr(gpio));
	dir &= ~mcfgpio_bit(gpio);
	mcfgpio_write(dir, __mcfgpio_pddr(gpio));
	local_irq_restore(flags);

	return 0;
}
EXPORT_SYMBOL(__mcfgpio_direction_input);

int __mcfgpio_direction_output(unsigned gpio, int value)
{
	unsigned long flags;
	MCFGPIO_PORTTYPE data;

	local_irq_save(flags);
	data = mcfgpio_read(__mcfgpio_pddr(gpio));
	data |= mcfgpio_bit(gpio);
	mcfgpio_write(data, __mcfgpio_pddr(gpio));

	/* now set the data to output */
	if (gpio < MCFGPIO_SCR_START) {
		data = mcfgpio_read(__mcfgpio_podr(gpio));
		if (value)
			data |= mcfgpio_bit(gpio);
		else
			data &= ~mcfgpio_bit(gpio);
		mcfgpio_write(data, __mcfgpio_podr(gpio));
	} else {
		 if (value)
			mcfgpio_write(mcfgpio_bit(gpio),
					MCFGPIO_SETR_PORT(gpio));
		 else
			 mcfgpio_write(~mcfgpio_bit(gpio),
					 MCFGPIO_CLRR_PORT(gpio));
	}
	local_irq_restore(flags);
	return 0;
}
EXPORT_SYMBOL(__mcfgpio_direction_output);

int __mcfgpio_request(unsigned gpio)
{
	return 0;
}
EXPORT_SYMBOL(__mcfgpio_request);

void __mcfgpio_free(unsigned gpio)
{
	__mcfgpio_direction_input(gpio);
}
EXPORT_SYMBOL(__mcfgpio_free);

#ifdef CONFIG_GPIOLIB

static int mcfgpio_direction_input(struct gpio_chip *chip, unsigned offset)
{
	return __mcfgpio_direction_input(offset);
}

static int mcfgpio_get_value(struct gpio_chip *chip, unsigned offset)
{
	return !!__mcfgpio_get_value(offset);
}

static int mcfgpio_direction_output(struct gpio_chip *chip, unsigned offset,
				    int value)
{
	return __mcfgpio_direction_output(offset, value);
}

static void mcfgpio_set_value(struct gpio_chip *chip, unsigned offset,
			      int value)
{
	__mcfgpio_set_value(offset, value);
}

static int mcfgpio_request(struct gpio_chip *chip, unsigned offset)
{
	return __mcfgpio_request(offset);
}

static void mcfgpio_free(struct gpio_chip *chip, unsigned offset)
{
	__mcfgpio_free(offset);
}

static int mcfgpio_to_irq(struct gpio_chip *chip, unsigned offset)
{
#if defined(MCFGPIO_IRQ_MIN)
	if ((offset >= MCFGPIO_IRQ_MIN) && (offset < MCFGPIO_IRQ_MAX))
#else
	if (offset < MCFGPIO_IRQ_MAX)
#endif
		return MCFGPIO_IRQ_VECBASE + offset;
	else
		return -EINVAL;
}

static struct bus_type mcfgpio_subsys = {
	.name		= "gpio",
	.dev_name	= "gpio",
};

static struct gpio_chip mcfgpio_chip = {
	.label			= "mcfgpio",
	.request		= mcfgpio_request,
	.free			= mcfgpio_free,
	.direction_input	= mcfgpio_direction_input,
	.direction_output	= mcfgpio_direction_output,
	.get			= mcfgpio_get_value,
	.set			= mcfgpio_set_value,
	.to_irq			= mcfgpio_to_irq,
	.base			= 0,
	.ngpio			= MCFGPIO_PIN_MAX,
};

static int __init mcfgpio_sysinit(void)
{
	gpiochip_add_data(&mcfgpio_chip, NULL);
	return subsys_system_register(&mcfgpio_subsys, NULL);
}

core_initcall(mcfgpio_sysinit);
#endif
