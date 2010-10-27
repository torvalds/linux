/*
 *  Copyright (C) 2010, Lars-Peter Clausen <lars@metafoo.de>
 *  JZ4740 platform timer support
 *
 *  This program is free software; you can redistribute it and/or modify it
 *  under  the terms of the GNU General  Public License as published by the
 *  Free Software Foundation;  either version 2 of the License, or (at your
 *  option) any later version.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  675 Mass Ave, Cambridge, MA 02139, USA.
 *
 */

#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/module.h>

#include "timer.h"

#include <asm/mach-jz4740/base.h>

void __iomem *jz4740_timer_base;

void jz4740_timer_enable_watchdog(void)
{
	writel(BIT(16), jz4740_timer_base + JZ_REG_TIMER_STOP_CLEAR);
}

void jz4740_timer_disable_watchdog(void)
{
	writel(BIT(16), jz4740_timer_base + JZ_REG_TIMER_STOP_SET);
}

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
