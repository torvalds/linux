// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *  Copyright (C) 2010, Lars-Peter Clausen <lars@metafoo.de>
 *  JZ4740 platform timer support
 */

#include <linux/export.h>
#include <linux/io.h>
#include <linux/init.h>
#include <linux/kernel.h>

#include <asm/mach-jz4740/base.h>
#include <asm/mach-jz4740/timer.h>

void __iomem *jz4740_timer_base;
EXPORT_SYMBOL_GPL(jz4740_timer_base);

void jz4740_timer_enable_watchdog(void)
{
	writel(BIT(16), jz4740_timer_base + JZ_REG_TIMER_STOP_CLEAR);
}
EXPORT_SYMBOL_GPL(jz4740_timer_enable_watchdog);

void jz4740_timer_disable_watchdog(void)
{
	writel(BIT(16), jz4740_timer_base + JZ_REG_TIMER_STOP_SET);
}
EXPORT_SYMBOL_GPL(jz4740_timer_disable_watchdog);

void __init jz4740_timer_init(void)
{
	jz4740_timer_base = ioremap(JZ4740_TCU_BASE_ADDR, 0x100);

	if (!jz4740_timer_base)
		panic("Failed to ioremap timer registers");

	/* Disable all timer clocks except for those used as system timers */
	writel(0x000100fc, jz4740_timer_base + JZ_REG_TIMER_STOP_SET);

	/* Timer irqs are unmasked by default, mask them */
	writel(0x00ff00ff, jz4740_timer_base + JZ_REG_TIMER_MASK_SET);
}
