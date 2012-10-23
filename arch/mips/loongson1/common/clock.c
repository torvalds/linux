/*
 * Copyright (c) 2011 Zhang, Keguang <keguang.zhang@gmail.com>
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */

#include <linux/clk.h>
#include <linux/err.h>
#include <asm/time.h>
#include <platform.h>

void __init plat_time_init(void)
{
	struct clk *clk;

	/* Initialize LS1X clocks */
	ls1x_clk_init();

	/* setup mips r4k timer */
	clk = clk_get(NULL, "cpu");
	if (IS_ERR(clk))
		panic("unable to get cpu clock, err=%ld", PTR_ERR(clk));

	mips_hpt_frequency = clk_get_rate(clk) / 2;
}
