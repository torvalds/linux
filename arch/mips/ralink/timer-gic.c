/*
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published
 * by the Free Software Foundation.
 *
 * Copyright (C) 2015 Nikolay Martynov <mar.kolya@gmail.com>
 * Copyright (C) 2015 John Crispin <john@phrozen.org>
 */

#include <linux/init.h>

#include <linux/of.h>
#include <linux/clk-provider.h>
#include <asm/time.h>

#include "common.h"

void __init plat_time_init(void)
{
	ralink_of_remap();
	ralink_clk_init();
	of_clk_init(NULL);
	timer_probe();
}
