// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (c) 2014 Zhang, Keguang <keguang.zhang@gmail.com>
 */

#include <linux/clk.h>
#include <linux/of_clk.h>
#include <asm/time.h>

void __init plat_time_init(void)
{
	struct clk *clk = NULL;

	/* initialize LS1X clocks */
	of_clk_init(NULL);

	/* setup mips r4k timer */
	clk = clk_get(NULL, "cpu_clk");
	if (IS_ERR(clk))
		panic("unable to get cpu clock, err=%ld", PTR_ERR(clk));

	mips_hpt_frequency = clk_get_rate(clk) / 2;
}
