/*
 *  linux/arch/arm/mach-aaec2000/clock.h
 *
 *  Copyright (C) 2005 Nicolas Bellido Y Ortega
 *
 *  Based on linux/arch/arm/mach-integrator/clock.h
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
struct module;

struct clk {
	struct list_head	node;
	unsigned long		rate;
	struct module		*owner;
	const char		*name;
	void			*data;
};

int clk_register(struct clk *clk);
void clk_unregister(struct clk *clk);
