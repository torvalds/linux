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

#ifndef __SERIAL_MCTRL_GPIO__
#define __SERIAL_MCTRL_GPIO__

#include <linux/err.h>
#include <linux/device.h>
#include <linux/gpio/consumer.h>

enum mctrl_gpio_idx {
	UART_GPIO_CTS,
	UART_GPIO_DSR,
	UART_GPIO_DCD,
	UART_GPIO_RNG,
	UART_GPIO_RI = UART_GPIO_RNG,
	UART_GPIO_RTS,
	UART_GPIO_DTR,
	UART_GPIO_OUT1,
	UART_GPIO_OUT2,
	UART_GPIO_MAX,
};

/*
 * Opaque descriptor for modem lines controlled by GPIOs
 */
struct mctrl_gpios;

#ifdef CONFIG_GPIOLIB

/*
 * Set state of the modem control output lines via GPIOs.
 */
void mctrl_gpio_set(struct mctrl_gpios *gpios, unsigned int mctrl);

/*
 * Get state of the modem control output lines from GPIOs.
 * The mctrl flags are updated and returned.
 */
unsigned int mctrl_gpio_get(struct mctrl_gpios *gpios, unsigned int *mctrl);

/*
 * Returns the associated struct gpio_desc to the modem line gidx
 */
struct gpio_desc *mctrl_gpio_to_gpiod(struct mctrl_gpios *gpios,
				      enum mctrl_gpio_idx gidx);

/*
 * Request and set direction of modem control lines GPIOs.
 * devm_* functions are used, so there's no need to call mctrl_gpio_free().
 * Returns a pointer to the allocated mctrl structure if ok, -ENOMEM on
 * allocation error.
 */
struct mctrl_gpios *mctrl_gpio_init(struct device *dev, unsigned int idx);

/*
 * Free the mctrl_gpios structure.
 * Normally, this function will not be called, as the GPIOs will
 * be disposed of by the resource management code.
 */
void mctrl_gpio_free(struct device *dev, struct mctrl_gpios *gpios);

#else /* GPIOLIB */

static inline
void mctrl_gpio_set(struct mctrl_gpios *gpios, unsigned int mctrl)
{
}

static inline
unsigned int mctrl_gpio_get(struct mctrl_gpios *gpios, unsigned int *mctrl)
{
	return *mctrl;
}

static inline
struct gpio_desc *mctrl_gpio_to_gpiod(struct mctrl_gpios *gpios,
				      enum mctrl_gpio_idx gidx)
{
	return ERR_PTR(-ENOSYS);
}

static inline
struct mctrl_gpios *mctrl_gpio_init(struct device *dev, unsigned int idx)
{
	return ERR_PTR(-ENOSYS);
}

static inline
void mctrl_gpio_free(struct device *dev, struct mctrl_gpios *gpios)
{
}

#endif /* GPIOLIB */

#endif
