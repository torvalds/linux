/*
 * arch/arm/mach-ns9xxx/gpio-ns9360.c
 *
 * Copyright (C) 2006,2007 by Digi International Inc.
 * All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 */
#include <linux/bug.h>
#include <linux/errno.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/module.h>

#include <mach/regs-bbu.h>
#include <mach/processor-ns9360.h>

#include "gpio-ns9360.h"

static inline int ns9360_valid_gpio(unsigned gpio)
{
	return gpio <= 72;
}

static inline void __iomem *ns9360_gpio_get_gconfaddr(unsigned gpio)
{
	if (gpio < 56)
		return BBU_GCONFb1(gpio / 8);
	else
		/*
		 * this could be optimised away on
		 * ns9750 only builds, but it isn't ...
		 */
		return BBU_GCONFb2((gpio - 56) / 8);
}

static inline void __iomem *ns9360_gpio_get_gctrladdr(unsigned gpio)
{
	if (gpio < 32)
		return BBU_GCTRL1;
	else if (gpio < 64)
		return BBU_GCTRL2;
	else
		/* this could be optimised away on ns9750 only builds */
		return BBU_GCTRL3;
}

static inline void __iomem *ns9360_gpio_get_gstataddr(unsigned gpio)
{
	if (gpio < 32)
		return BBU_GSTAT1;
	else if (gpio < 64)
		return BBU_GSTAT2;
	else
		/* this could be optimised away on ns9750 only builds */
		return BBU_GSTAT3;
}

/*
 * each gpio can serve for 4 different purposes [0..3].  These are called
 * "functions" and passed in the parameter func.  Functions 0-2 are always some
 * special things, function 3 is GPIO.  If func == 3 dir specifies input or
 * output, and with inv you can enable an inverter (independent of func).
 */
int __ns9360_gpio_configure(unsigned gpio, int dir, int inv, int func)
{
	void __iomem *conf = ns9360_gpio_get_gconfaddr(gpio);
	u32 confval;

	confval = __raw_readl(conf);
	REGSETIM_IDX(confval, BBU_GCONFx, DIR, gpio & 7, dir);
	REGSETIM_IDX(confval, BBU_GCONFx, INV, gpio & 7, inv);
	REGSETIM_IDX(confval, BBU_GCONFx, FUNC, gpio & 7, func);
	__raw_writel(confval, conf);

	return 0;
}

int ns9360_gpio_configure(unsigned gpio, int inv, int func)
{
	if (likely(ns9360_valid_gpio(gpio))) {
		if (func == 3) {
			printk(KERN_WARNING "use gpio_direction_input "
					"or gpio_direction_output\n");
			return -EINVAL;
		} else
			return __ns9360_gpio_configure(gpio, 0, inv, func);
	} else
		return -EINVAL;
}
EXPORT_SYMBOL(ns9360_gpio_configure);

int ns9360_gpio_get_value(unsigned gpio)
{
	void __iomem *stat = ns9360_gpio_get_gstataddr(gpio);
	int ret;

	ret = 1 & (__raw_readl(stat) >> (gpio & 31));

	return ret;
}

void ns9360_gpio_set_value(unsigned gpio, int value)
{
	void __iomem *ctrl = ns9360_gpio_get_gctrladdr(gpio);
	u32 ctrlval;

	ctrlval = __raw_readl(ctrl);

	if (value)
		ctrlval |= 1 << (gpio & 31);
	else
		ctrlval &= ~(1 << (gpio & 31));

	__raw_writel(ctrlval, ctrl);
}
