/*
 * arch/arm/mach-tegra/include/mach/clkdev.h
 *
 * Copyright (C) 2010 Google, Inc.
 *
 * Author:
 *	Colin Cross <ccross@google.com>
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#ifndef __MACH_CLKDEV_H
#define __MACH_CLKDEV_H

static inline int __clk_get(struct clk *clk)
{
	return 1;
}

static inline void __clk_put(struct clk *clk)
{
}

#endif
