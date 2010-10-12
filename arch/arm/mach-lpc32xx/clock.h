/*
 * arch/arm/mach-lpc32xx/clock.h
 *
 * Author: Kevin Wells <kevin.wells@nxp.com>
 *
 * Copyright (C) 2010 NXP Semiconductors
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#ifndef __LPC32XX_CLOCK_H
#define __LPC32XX_CLOCK_H

struct clk {
	struct list_head node;
	struct clk *parent;
	u32 rate;
	u32 usecount;

	int (*set_rate) (struct clk *, unsigned long);
	unsigned long (*round_rate) (struct clk *, unsigned long);
	unsigned long (*get_rate) (struct clk *clk);
	int (*enable) (struct clk *, int);

	/* Register address and bit mask for simple clocks */
	void __iomem *enable_reg;
	u32 enable_mask;
};

#endif
