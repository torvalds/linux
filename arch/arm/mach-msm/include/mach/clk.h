/* Copyright (c) 2009, Code Aurora Forum. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */
#ifndef __MACH_CLK_H
#define __MACH_CLK_H

/* Magic rate value for use with PM QOS to request the board's maximum
 * supported AXI rate. PM QOS will only pass positive s32 rate values
 * through to the clock driver, so INT_MAX is used.
 */
#define MSM_AXI_MAX_FREQ	LONG_MAX

enum clk_reset_action {
	CLK_RESET_DEASSERT	= 0,
	CLK_RESET_ASSERT	= 1
};

struct clk;

/* Rate is minimum clock rate in Hz */
int clk_set_min_rate(struct clk *clk, unsigned long rate);

/* Rate is maximum clock rate in Hz */
int clk_set_max_rate(struct clk *clk, unsigned long rate);

/* Assert/Deassert reset to a hardware block associated with a clock */
int clk_reset(struct clk *clk, enum clk_reset_action action);

#endif
