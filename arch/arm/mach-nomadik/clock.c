/*
 *  linux/arch/arm/mach-nomadik/clock.c
 *
 *  Copyright (C) 2009 Alessandro Rubini
 */
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/errno.h>
#include <linux/clk.h>
#include <asm/clkdev.h>
#include "clock.h"

/*
 * The nomadik board uses generic clocks, but the serial pl011 file
 * calls clk_enable(), clk_disable(), clk_get_rate(), so we provide them
 */
unsigned long clk_get_rate(struct clk *clk)
{
	return clk->rate;
}
EXPORT_SYMBOL(clk_get_rate);

/* enable and disable do nothing */
int clk_enable(struct clk *clk)
{
	return 0;
}
EXPORT_SYMBOL(clk_enable);

void clk_disable(struct clk *clk)
{
}
EXPORT_SYMBOL(clk_disable);

/* We have a fixed clock alone, for now */
static struct clk clk_48 = {
	.rate = 48 * 1000 * 1000,
};

/*
 * Catch-all default clock to satisfy drivers using the clk API.  We don't
 * model the actual hardware clocks yet.
 */
static struct clk clk_default;

#define CLK(_clk, dev)				\
	{					\
		.clk		= _clk,		\
		.dev_id		= dev,		\
	}

static struct clk_lookup lookups[] = {
	CLK(&clk_48, "uart0"),
	CLK(&clk_48, "uart1"),
	CLK(&clk_default, "gpio.0"),
	CLK(&clk_default, "gpio.1"),
	CLK(&clk_default, "gpio.2"),
	CLK(&clk_default, "gpio.3"),
	CLK(&clk_default, "rng"),
};

static int __init clk_init(void)
{
	clkdev_add_table(lookups, ARRAY_SIZE(lookups));
	return 0;
}

arch_initcall(clk_init);
