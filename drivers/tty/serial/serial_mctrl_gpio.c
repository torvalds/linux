/*
 * Helpers for controlling modem lines via GPIO
 *
 * Copyright (C) 2014 Paratronic S.A.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#include <linux/err.h>
#include <linux/device.h>
#include <linux/gpio/consumer.h>
#include <linux/termios.h>

#include "serial_mctrl_gpio.h"

struct mctrl_gpios {
	struct gpio_desc *gpio[UART_GPIO_MAX];
};

static const struct {
	const char *name;
	unsigned int mctrl;
	bool dir_out;
} mctrl_gpios_desc[UART_GPIO_MAX] = {
	{ "cts", TIOCM_CTS, false, },
	{ "dsr", TIOCM_DSR, false, },
	{ "dcd", TIOCM_CD, false, },
	{ "rng", TIOCM_RNG, false, },
	{ "rts", TIOCM_RTS, true, },
	{ "dtr", TIOCM_DTR, true, },
	{ "out1", TIOCM_OUT1, true, },
	{ "out2", TIOCM_OUT2, true, },
};

void mctrl_gpio_set(struct mctrl_gpios *gpios, unsigned int mctrl)
{
	enum mctrl_gpio_idx i;
	struct gpio_desc *desc_array[UART_GPIO_MAX];
	int value_array[UART_GPIO_MAX];
	unsigned int count = 0;

	for (i = 0; i < UART_GPIO_MAX; i++)
		if (!IS_ERR_OR_NULL(gpios->gpio[i]) &&
		    mctrl_gpios_desc[i].dir_out) {
			desc_array[count] = gpios->gpio[i];
			value_array[count] = !!(mctrl & mctrl_gpios_desc[i].mctrl);
			count++;
		}
	gpiod_set_array(count, desc_array, value_array);
}
EXPORT_SYMBOL_GPL(mctrl_gpio_set);

struct gpio_desc *mctrl_gpio_to_gpiod(struct mctrl_gpios *gpios,
				      enum mctrl_gpio_idx gidx)
{
	return gpios->gpio[gidx];
}
EXPORT_SYMBOL_GPL(mctrl_gpio_to_gpiod);

unsigned int mctrl_gpio_get(struct mctrl_gpios *gpios, unsigned int *mctrl)
{
	enum mctrl_gpio_idx i;

	for (i = 0; i < UART_GPIO_MAX; i++) {
		if (gpios->gpio[i] && !mctrl_gpios_desc[i].dir_out) {
			if (gpiod_get_value(gpios->gpio[i]))
				*mctrl |= mctrl_gpios_desc[i].mctrl;
			else
				*mctrl &= ~mctrl_gpios_desc[i].mctrl;
		}
	}

	return *mctrl;
}
EXPORT_SYMBOL_GPL(mctrl_gpio_get);

struct mctrl_gpios *mctrl_gpio_init(struct device *dev, unsigned int idx)
{
	struct mctrl_gpios *gpios;
	enum mctrl_gpio_idx i;

	gpios = devm_kzalloc(dev, sizeof(*gpios), GFP_KERNEL);
	if (!gpios)
		return ERR_PTR(-ENOMEM);

	for (i = 0; i < UART_GPIO_MAX; i++) {
		enum gpiod_flags flags;

		if (mctrl_gpios_desc[i].dir_out)
			flags = GPIOD_OUT_LOW;
		else
			flags = GPIOD_IN;

		gpios->gpio[i] =
			devm_gpiod_get_index_optional(dev,
						      mctrl_gpios_desc[i].name,
						      idx, flags);

		if (IS_ERR(gpios->gpio[i]))
			return ERR_CAST(gpios->gpio[i]);
	}

	return gpios;
}
EXPORT_SYMBOL_GPL(mctrl_gpio_init);

void mctrl_gpio_free(struct device *dev, struct mctrl_gpios *gpios)
{
	enum mctrl_gpio_idx i;

	for (i = 0; i < UART_GPIO_MAX; i++)
		if (!IS_ERR_OR_NULL(gpios->gpio[i]))
			devm_gpiod_put(dev, gpios->gpio[i]);
	devm_kfree(dev, gpios);
}
EXPORT_SYMBOL_GPL(mctrl_gpio_free);
