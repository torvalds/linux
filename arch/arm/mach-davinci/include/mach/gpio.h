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

#include <linux/io.h>
#include <asm-generic/gpio.h>

#include <mach/irqs.h>
#include <mach/common.h>

#define DAVINCI_GPIO_BASE 0x01C67000

/*
 * basic gpio routines
 *
 * board-specific init should be done by arch/.../.../board-XXX.c (maybe
 * initializing banks together) rather than boot loaders; kexec() won't
 * go through boot loaders.
 *
 * the gpio clock will be turned on when gpios are used, and you may also
 * need to pay attention to PINMUX registers to be sure those pins are
 * used as gpios, not with other peripherals.
 *
 * On-chip GPIOs are numbered 0..(DAVINCI_N_GPIO-1).  For documentation,
 * and maybe for later updates, code may write GPIO(N).  These may be
 * all 1.8V signals, all 3.3V ones, or a mix of the two.  A given chip
 * may not support all the GPIOs in that range.
 *
 * GPIOs can also be on external chips, numbered after the ones built-in
 * to the DaVinci chip.  For now, they won't be usable as IRQ sources.
 */
#define	GPIO(X)		(X)		/* 0 <= X <= (DAVINCI_N_GPIO - 1) */

/* Convert GPIO signal to GPIO pin number */
#define GPIO_TO_PIN(bank, gpio)	(16 * (bank) + (gpio))

struct gpio_controller {
	u32	dir;
	u32	out_data;
	u32	set_data;
	u32	clr_data;
	u32	in_data;
	u32	set_rising;
	u32	clr_rising;
	u32	set_falling;
	u32	clr_falling;
	u32	intstat;
};

/* The __gpio_to_controller() and __gpio_mask() functions inline to constants
 * with constant parameters; or in outlined code they execute at runtime.
 *
 * You'd access the controller directly when reading or writing more than
 * one gpio value at a time, and to support wired logic where the value
 * being driven by the cpu need not match the value read back.
 *
 * These are NOT part of the cross-platform GPIO interface
 */
static inline struct gpio_controller *__iomem
__gpio_to_controller(unsigned gpio)
{
	void *__iomem ptr;
	void __iomem *base = davinci_soc_info.gpio_base;

	if (gpio < 32 * 1)
		ptr = base + 0x10;
	else if (gpio < 32 * 2)
		ptr = base + 0x38;
	else if (gpio < 32 * 3)
		ptr = base + 0x60;
	else if (gpio < 32 * 4)
		ptr = base + 0x88;
	else if (gpio < 32 * 5)
		ptr = base + 0xb0;
	else
		ptr = NULL;
	return ptr;
}

static inline u32 __gpio_mask(unsigned gpio)
{
	return 1 << (gpio % 32);
}

/* The get/set/clear functions will inline when called with constant
 * parameters referencing built-in GPIOs, for low-overhead bitbanging.
 *
 * Otherwise, calls with variable parameters or referencing external
 * GPIOs (e.g. on GPIO expander chips) use outlined functions.
 */
static inline void gpio_set_value(unsigned gpio, int value)
{
	if (__builtin_constant_p(value) && gpio < DAVINCI_N_GPIO) {
		struct gpio_controller	*__iomem g;
		u32			mask;

		g = __gpio_to_controller(gpio);
		mask = __gpio_mask(gpio);
		if (value)
			__raw_writel(mask, &g->set_data);
		else
			__raw_writel(mask, &g->clr_data);
		return;
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
	struct gpio_controller	*__iomem g;

	if (!__builtin_constant_p(gpio) || gpio >= DAVINCI_N_GPIO)
		return __gpio_get_value(gpio);

	g = __gpio_to_controller(gpio);
	return __gpio_mask(gpio) & __raw_readl(&g->in_data);
}

static inline int gpio_cansleep(unsigned gpio)
{
	if (__builtin_constant_p(gpio) && gpio < DAVINCI_N_GPIO)
		return 0;
	else
		return __gpio_cansleep(gpio);
}

static inline int gpio_to_irq(unsigned gpio)
{
	return __gpio_to_irq(gpio);
}

static inline int irq_to_gpio(unsigned irq)
{
	/* don't support the reverse mapping */
	return -ENOSYS;
}

#endif				/* __DAVINCI_GPIO_H */
