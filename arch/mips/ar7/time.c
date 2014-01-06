/*
 * Carsten Langgaard, carstenl@mips.com
 * Copyright (C) 1999,2000 MIPS Technologies, Inc.  All rights reserved.
 *
 *  This program is free software; you can distribute it and/or modify it
 *  under the terms of the GNU General Public License (Version 2) as
 *  published by the Free Software Foundation.
 *
 *  This program is distributed in the hope it will be useful, but WITHOUT
 *  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 *  FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 *  for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  59 Temple Place - Suite 330, Boston MA 02111-1307, USA.
 *
 * Setting up the clock on the MIPS boards.
 */

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
