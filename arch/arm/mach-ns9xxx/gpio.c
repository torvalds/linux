/*
 * arch/arm/mach-ns9xxx/gpio.c
 *
 * Copyright (C) 2006 by Digi International Inc.
 * All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 */
#include <linux/compiler.h>
#include <linux/init.h>
#include <linux/spinlock.h>
#include <linux/module.h>

#include <asm/arch-ns9xxx/gpio.h>
#include <asm/arch-ns9xxx/processor.h>
#include <asm/arch-ns9xxx/regs-bbu.h>
#include <asm/io.h>
#include <asm/bug.h>
#include <asm/types.h>
#include <asm/bitops.h>

#if defined(CONFIG_PROCESSOR_NS9360)
#define GPIO_MAX 72
#elif defined(CONFIG_PROCESSOR_NS9750)
#define GPIO_MAX 49
#endif

/* protects BBU_GCONFx and BBU_GCTRLx */
static spinlock_t gpio_lock = __SPIN_LOCK_UNLOCKED(gpio_lock);

/* only access gpiores with atomic ops */
static DECLARE_BITMAP(gpiores, GPIO_MAX);

static inline int ns9xxx_valid_gpio(unsigned gpio)
{
#if defined(CONFIG_PROCESSOR_NS9360)
	if (processor_is_ns9360())
		return gpio <= 72;
	else
#endif
#if defined(CONFIG_PROCESSOR_NS9750)
	if (processor_is_ns9750())
		return gpio <= 49;
	else
#endif
		BUG();
}

static inline void __iomem *ns9xxx_gpio_get_gconfaddr(unsigned gpio)
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

static inline void __iomem *ns9xxx_gpio_get_gctrladdr(unsigned gpio)
{
	if (gpio < 32)
		return BBU_GCTRL1;
	else if (gpio < 64)
		return BBU_GCTRL2;
	else
		/* this could be optimised away on ns9750 only builds */
		return BBU_GCTRL3;
}

static inline void __iomem *ns9xxx_gpio_get_gstataddr(unsigned gpio)
{
	if (gpio < 32)
		return BBU_GSTAT1;
	else if (gpio < 64)
		return BBU_GSTAT2;
	else
		/* this could be optimised away on ns9750 only builds */
		return BBU_GSTAT3;
}

int gpio_request(unsigned gpio, const char *label)
{
	if (likely(ns9xxx_valid_gpio(gpio)))
		return test_and_set_bit(gpio, gpiores) ? -EBUSY : 0;
	else
		return -EINVAL;
}
EXPORT_SYMBOL(gpio_request);

void gpio_free(unsigned gpio)
{
	clear_bit(gpio, gpiores);
	return;
}
EXPORT_SYMBOL(gpio_free);

/*
 * each gpio can serve for 4 different purposes [0..3].  These are called
 * "functions" and passed in the parameter func.  Functions 0-2 are always some
 * special things, function 3 is GPIO.  If func == 3 dir specifies input or
 * output, and with inv you can enable an inverter (independent of func).
 */
static int __ns9xxx_gpio_configure(unsigned gpio, int dir, int inv, int func)
{
	void __iomem *conf = ns9xxx_gpio_get_gconfaddr(gpio);
	u32 confval;
	unsigned long flags;

	spin_lock_irqsave(&gpio_lock, flags);

	confval = __raw_readl(conf);
	REGSETIM_IDX(confval, BBU_GCONFx, DIR, gpio & 7, dir);
	REGSETIM_IDX(confval, BBU_GCONFx, INV, gpio & 7, inv);
	REGSETIM_IDX(confval, BBU_GCONFx, FUNC, gpio & 7, func);
	__raw_writel(confval, conf);

	spin_unlock_irqrestore(&gpio_lock, flags);

	return 0;
}

int ns9xxx_gpio_configure(unsigned gpio, int inv, int func)
{
	if (likely(ns9xxx_valid_gpio(gpio))) {
		if (func == 3) {
			printk(KERN_WARNING "use gpio_direction_input "
					"or gpio_direction_output\n");
			return -EINVAL;
		} else
			return __ns9xxx_gpio_configure(gpio, 0, inv, func);
	} else
		return -EINVAL;
}
EXPORT_SYMBOL(ns9xxx_gpio_configure);

int gpio_direction_input(unsigned gpio)
{
	if (likely(ns9xxx_valid_gpio(gpio))) {
		return __ns9xxx_gpio_configure(gpio, 0, 0, 3);
	} else
		return -EINVAL;
}
EXPORT_SYMBOL(gpio_direction_input);

int gpio_direction_output(unsigned gpio, int value)
{
	if (likely(ns9xxx_valid_gpio(gpio))) {
		gpio_set_value(gpio, value);

		return __ns9xxx_gpio_configure(gpio, 1, 0, 3);
	} else
		return -EINVAL;
}
EXPORT_SYMBOL(gpio_direction_output);

int gpio_get_value(unsigned gpio)
{
	void __iomem *stat = ns9xxx_gpio_get_gstataddr(gpio);
	int ret;

	ret = 1 & (__raw_readl(stat) >> (gpio & 31));

	return ret;
}
EXPORT_SYMBOL(gpio_get_value);

void gpio_set_value(unsigned gpio, int value)
{
	void __iomem *ctrl = ns9xxx_gpio_get_gctrladdr(gpio);
	u32 ctrlval;
	unsigned long flags;

	spin_lock_irqsave(&gpio_lock, flags);

	ctrlval = __raw_readl(ctrl);

	if (value)
		ctrlval |= 1 << (gpio & 31);
	else
		ctrlval &= ~(1 << (gpio & 31));

	__raw_writel(ctrlval, ctrl);

	spin_unlock_irqrestore(&gpio_lock, flags);
}
EXPORT_SYMBOL(gpio_set_value);
