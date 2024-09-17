// SPDX-License-Identifier: GPL-2.0-only
/*
 *
 * Copyright (C) 2015 Nikolay Martynov <mar.kolya@gmail.com>
 * Copyright (C) 2015 John Crispin <john@phrozen.org>
 */

#include <linux/init.h>

#include <linux/of.h>
#include <linux/of_clk.h>
#include <linux/clocksource.h>

#include <asm/time.h>

#include "common.h"

void __init plat_time_init(void)
{
	ralink_of_remap();

	of_clk_init(NULL);
	timer_probe();
}
