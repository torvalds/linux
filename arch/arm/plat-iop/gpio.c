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
#include <asm/hardware/iop3xx.h>

void gpio_line_config(int line, int direction)
{
	unsigned long flags;

	local_irq_save(flags);
	if (direction == GPIO_IN) {
		*IOP3XX_GPOE |= 1 << line;
	} else if (direction == GPIO_OUT) {
		*IOP3XX_GPOE &= ~(1 << line);
	}
	local_irq_restore(flags);
}
EXPORT_SYMBOL(gpio_line_config);

int gpio_line_get(int line)
{
	return !!(*IOP3XX_GPID & (1 << line));
}
EXPORT_SYMBOL(gpio_line_get);

void gpio_line_set(int line, int value)
{
	unsigned long flags;

	local_irq_save(flags);
	if (value == GPIO_LOW) {
		*IOP3XX_GPOD &= ~(1 << line);
	} else if (value == GPIO_HIGH) {
		*IOP3XX_GPOD |= 1 << line;
	}
	local_irq_restore(flags);
}
EXPORT_SYMBOL(gpio_line_set);
