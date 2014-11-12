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

	if (IS_ERR_OR_NULL(gpios))
		return;

	for (i = 0; i < UART_GPIO_MAX; i++)
		if (!IS_ERR_OR_NULL(gpios->gpio[i]) &&
		    mctrl_gpios_desc[i].dir_out)
			gpiod_set_value(gpios->gpio[i],
					!!(mctrl & mctrl_gpios_desc[i].mctrl));
}
EXPORT_SYMBOL_GPL(mctrl_gpio_set);

struct gpio_desc *mctrl_gpio_to_gpiod(struct mctrl_gpios *gpios,
				      enum mctrl_gpio_idx gidx)
{
	if (!IS_ERR_OR_NULL(gpios) && !IS_ERR_OR_NULL(gpios->gpio[gidx]))
		return gpios->gpio[gidx];
	else
		return NULL;
}
EXPORT_SYMBOL_GPL(mctrl_gpio_to_gpiod);

unsigned int mctrl_gpio_get(struct mctrl_gpios *gpios, unsigned int *mctrl)
{
	enum mctrl_gpio_idx i;

	/*
	 * return it unchanged if the structure is not allocated
	 */
	if (IS_ERR_OR_NULL(gpios))
		return *mctrl;

	for (i = 0; i < UART_GPIO_MAX; i++) {
		if (!IS_ERR_OR_NULL(gpios->gpio[i]) &&
		    !mctrl_gpios_desc[i].dir_out) {
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
	int err;

	gpios = devm_kzalloc(dev, sizeof(*gpios), GFP_KERNEL);
	if (!gpios)
		return ERR_PTR(-ENOMEM);

	for (i = 0; i < UART_GPIO_MAX; i++) {
		gpios->gpio[i] = devm_gpiod_get_index(dev,
						      mctrl_gpios_desc[i].name,
						      idx);

		/*
		 * The GPIOs are maybe not all filled,
		 * this is not an error.
		 */
		if (IS_ERR_OR_NULL(gpios->gpio[i]))
			continue;

		if (mctrl_gpios_desc[i].dir_out)
			err = gpiod_direction_output(gpios->gpio[i], 0);
		else
			err = gpiod_direction_input(gpios->gpio[i]);
		if (err) {
			dev_dbg(dev, "Unable to set direction for %s GPIO",
				mctrl_gpios_desc[i].name);
			devm_gpiod_put(dev, gpios->gpio[i]);
			gpios->gpio[i] = NULL;
		}
	}

	return gpios;
}
EXPORT_SYMBOL_GPL(mctrl_gpio_init);

void mctrl_gpio_free(struct device *dev, struct mctrl_gpios *gpios)
{
	enum mctrl_gpio_idx i;

	if (IS_ERR_OR_NULL(gpios))
		return;

	for (i = 0; i < UART_GPIO_MAX; i++)
		if (!IS_ERR_OR_NULL(gpios->gpio[i]))
			devm_gpiod_put(dev, gpios->gpio[i]);
	devm_kfree(dev, gpios);
}
EXPORT_SYMBOL_GPL(mctrl_gpio_free);
