// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *  Copyright (C) 2010, Lars-Peter Clausen <lars@metafoo.de>
 *  JZ4740 platform time support
 */

#include <linux/clk-provider.h>
#include <linux/clocksource.h>

#include <asm/mach-jz4740/timer.h>

void __init plat_time_init(void)
{
	of_clk_init(NULL);
	jz4740_timer_init();
	timer_probe();
}
