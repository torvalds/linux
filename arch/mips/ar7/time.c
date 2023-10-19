// SPDX-License-Identifier: GPL-2.0-only
/*
 * Carsten Langgaard, carstenl@mips.com
 * Copyright (C) 1999,2000 MIPS Technologies, Inc.  All rights reserved.
 *
 * Setting up the clock on the MIPS boards.
 */

#include <linux/init.h>
#include <linux/time.h>
#include <linux/err.h>
#include <linux/clk.h>

#include <asm/time.h>
#include <asm/mach-ar7/ar7.h>

void __init plat_time_init(void)
{
	struct clk *cpu_clk;

	/* Initialize ar7 clocks so the CPU clock frequency is correct */
	ar7_init_clocks();

	cpu_clk = clk_get(NULL, "cpu");
	if (IS_ERR(cpu_clk)) {
		printk(KERN_ERR "unable to get cpu clock\n");
		return;
	}

	mips_hpt_frequency = clk_get_rate(cpu_clk) / 2;
}
