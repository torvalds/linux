
/*
 *  linux/arch/arm/mach-nomadik/clock.h
 *
 *  Copyright (C) 2009 Alessandro Rubini
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
struct clk {
	unsigned long		rate;
};
extern int nmdk_clk_create(struct clk *clk, const char *dev_id);
