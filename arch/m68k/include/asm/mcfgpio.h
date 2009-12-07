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

#ifndef mcfgpio_h
#define mcfgpio_h

#include <linux/io.h>
#include <asm-generic/gpio.h>

struct mcf_gpio_chip {
	struct gpio_chip gpio_chip;
	void __iomem *pddr;
	void __iomem *podr;
	void __iomem *ppdr;
	void __iomem *setr;
	void __iomem *clrr;
	const u8 *gpio_to_pinmux;
};

int mcf_gpio_direction_input(struct gpio_chip *, unsigned);
int mcf_gpio_get_value(struct gpio_chip *, unsigned);
int mcf_gpio_direction_output(struct gpio_chip *, unsigned, int);
void mcf_gpio_set_value(struct gpio_chip *, unsigned, int);
void mcf_gpio_set_value_fast(struct gpio_chip *, unsigned, int);
int mcf_gpio_request(struct gpio_chip *, unsigned);
void mcf_gpio_free(struct gpio_chip *, unsigned);

#endif
