/*
 * TI DaVinci GPIO Support
 *
 * Copyright (c) 2006 David Brownell
 * Copyright (c) 2007, MontaVista Software, Inc. <source@mvista.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#ifndef	__DAVINCI_GPIO_H
#define	__DAVINCI_GPIO_H

#include <asm-generic/gpio.h>

#define __ARM_GPIOLIB_COMPLEX

/* The inline versions use the static inlines in the driver header */
#include "gpio-davinci.h"

/*
 * The get/set/clear functions will inline when called with constant
 * parameters referencing built-in GPIOs, for low-overhead bitbanging.
 *
 * gpio_set_value() will inline only on traditional Davinci style controllers
 * with distinct set/clear registers.
 *
 * Otherwise, calls with variable parameters or referencing external
 * GPIOs (e.g. on GPIO expander chips) use outlined functions.
 */
static inline void gpio_set_value(unsigned gpio, int value)
{
	if (__builtin_constant_p(value) && gpio < davinci_soc_info.gpio_num) {
		struct davinci_gpio_controller *ctlr;
		u32				mask;

		ctlr = __gpio_to_controller(gpio);

		if (ctlr->set_data != ctlr->clr_data) {
			mask = __gpio_mask(gpio);
			if (value)
				__raw_writel(mask, ctlr->set_data);
			else
				__raw_writel(mask, ctlr->clr_data);
			return;
		}
	}

	__gpio_set_value(gpio, value);
}

/* Returns zero or nonzero; works for gpios configured as inputs OR
 * as outputs, at least for built-in GPIOs.
 *
 * NOTE: for built-in GPIOs, changes in reported values are synchronized
 * to the GPIO clock.  This is easily seen after calling gpio_set_value()
 * and then immediately gpio_get_value(), where the gpio_get_value() will
 * return the old value until the GPIO clock ticks and the new value gets
 * latched.
 */
static inline int gpio_get_value(unsigned gpio)
{
	struct davinci_gpio_controller *ctlr;

	if (!__builtin_constant_p(gpio) || gpio >= davinci_soc_info.gpio_num)
		return __gpio_get_value(gpio);

	ctlr = __gpio_to_controller(gpio);
	return __gpio_mask(gpio) & __raw_readl(ctlr->in_data);
}

static inline int gpio_cansleep(unsigned gpio)
{
	if (__builtin_constant_p(gpio) && gpio < davinci_soc_info.gpio_num)
		return 0;
	else
		return __gpio_cansleep(gpio);
}

static inline int irq_to_gpio(unsigned irq)
{
	/* don't support the reverse mapping */
	return -ENOSYS;
}

#endif				/* __DAVINCI_GPIO_H */
