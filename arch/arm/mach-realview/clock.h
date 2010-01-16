/*
 *  linux/arch/arm/mach-realview/clock.h
 *
 *  Copyright (C) 2004 ARM Limited.
 *  Written by Deep Blue Solutions Limited.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <asm/hardware/icst.h>

struct module;

struct clk {
	unsigned long		rate;
	const struct icst_params *params;
	void			*data;
	void			(*setvco)(struct clk *, struct icst_vco vco);
};
