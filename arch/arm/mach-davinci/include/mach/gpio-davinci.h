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

#ifndef	__DAVINCI_DAVINCI_GPIO_H
#define	__DAVINCI_DAVINCI_GPIO_H

#include <linux/io.h>
#include <linux/spinlock.h>

#include <asm-generic/gpio.h>

#include <mach/irqs.h>
#include <mach/common.h>

#define DAVINCI_GPIO_BASE 0x01C67000

enum davinci_gpio_type {
	GPIO_TYPE_DAVINCI = 0,
	GPIO_TYPE_TNETV107X,
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

#endif	/* __DAVINCI_DAVINCI_GPIO_H */
