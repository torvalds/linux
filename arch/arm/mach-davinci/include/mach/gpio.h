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
#include <linux/spinlock.h>

#include <asm-generic/gpio.h>

#include <mach/irqs.h>
#include <mach/common.h>

#define DAVINCI_GPIO_BASE 0x01C67000

enum davinci_gpio_type {
	GPIO_TYPE_DAVINCI = 0,
};

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

struct davinci_gpio_controller {
	struct gpio_chip	chip;
	int			irq_base;
	spinlock_t		lock;
	void __iomem		*regs;
	void __iomem		*set_data;
	void __iomem		*clr_data;
	void __iomem		*in_data;
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
static inline struct davinci_gpio_controller *
__gpio_to_controller(unsigned gpio)
{
	struct davinci_gpio_controller *ctlrs = davinci_soc_info.gpio_ctlrs;
	int index = gpio / 32;

	if (!ctlrs || index >= davinci_soc_info.gpio_ctlrs_num)
		return NULL;

	return ctlrs + index;
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
	if (__builtin_constant_p(value) && gpio < davinci_soc_info.gpio_num) {
		struct davinci_gpio_controller *ctlr;
		u32				mask;

		ctlr = __gpio_to_controller(gpio);
		mask = __gpio_mask(gpio);
		if (value)
			__raw_writel(mask, ctlr->set_data);
		else
			__raw_writel(mask, ctlr->clr_data);
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
