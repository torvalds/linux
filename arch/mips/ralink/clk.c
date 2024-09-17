// SPDX-License-Identifier: GPL-2.0-only
/*
 *
 *  Copyright (C) 2011 Gabor Juhos <juhosg@openwrt.org>
 *  Copyright (C) 2013 John Crispin <john@phrozen.org>
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/export.h>
#include <linux/clkdev.h>
#include <linux/clk.h>
#include <linux/clk-provider.h>

#include <asm/time.h>

#include "common.h"

void ralink_clk_add(const char *dev, unsigned long rate)
{
	struct clk *clk = clk_register_fixed_rate(NULL, dev, NULL, 0, rate);

	if (!clk)
		panic("failed to add clock");

	clkdev_create(clk, NULL, "%s", dev);
}

void __init plat_time_init(void)
{
	struct clk *clk;

	ralink_of_remap();

	ralink_clk_init();
	clk = clk_get_sys("cpu", NULL);
	if (IS_ERR(clk))
		panic("unable to get CPU clock, err=%ld", PTR_ERR(clk));
	pr_info("CPU Clock: %ldMHz\n", clk_get_rate(clk) / 1000000);
	mips_hpt_frequency = clk_get_rate(clk) / 2;
	clk_put(clk);
	timer_probe();
}
